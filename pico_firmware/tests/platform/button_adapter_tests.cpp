// Compiles the production GPIO adapter against a controlled workqueue/GPIO model.
// This checks adapter behavior; it is not a Zephyr or electrical validation.
#include "platform/pico_button.hpp"
#include "platform/pico_status_led.hpp"
#include <pico/status_led.hpp>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

namespace {
std::uint64_t now;
std::vector<k_work_delayable *> jobs;
std::array<gpio_callback *, 8> callbacks{};
std::array<int, 8> levels{};
std::vector<pico::EventKind> events;
unsigned reads;
int write_error;
std::vector<std::pair<std::uint64_t,int>> led_writes;
bool in_isr;
bool controller_ready = true;
int configure_error, add_error, interrupt_error;
std::function<void()> read_hook;
unsigned failures;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " #expr "\n"; ++failures; } } while (false)

bool sink(const pico::Event &event)
{
    CHECK(!in_isr);
    events.push_back(event.kind);
    return true;
}
void edge(unsigned pin, int level)
{
    levels[pin] = level;
    in_isr = true;
    if (callbacks[pin]) callbacks[pin]->handler(nullptr, callbacks[pin], 1U << pin);
    in_isr = false;
}
void advance(std::uint64_t end)
{
    unsigned dispatches = 0;
    while (true) {
        k_work_delayable *next = nullptr;
        for (auto *job : jobs)
            if (job->pending && job->due <= end && (!next || job->due < next->due)) next = job;
        if (!next) break;
        if (++dispatches > 100) { CHECK(false); break; }
        now = next->due;
        next->pending = false;
        next->work.handler(&next->work);
    }
    now = end;
}
void reset(std::uint64_t time = 0)
{
    now = time;
    jobs.clear(); callbacks.fill(nullptr); levels.fill(0); events.clear();
    reads = 0; in_isr = false; controller_ready = true; write_error=0; led_writes.clear();
    configure_error = add_error = interrupt_error = 0; read_hook = {};
}
const device port{};
const gpio_dt_spec pin{&port, 2, 0};
}

void k_work_init_delayable(k_work_delayable *job, void (*handler)(k_work *))
{ job->work.handler = handler; jobs.push_back(job); }
int k_work_schedule(k_work_delayable *job, k_timeout_t delay)
{ if (job->pending) return 0; job->pending = true; job->due = now + delay; return 1; }
int k_work_reschedule(k_work_delayable *job, k_timeout_t delay)
{ job->pending = true; job->due = now + delay; return 1; }
k_work_delayable *k_work_delayable_from_work(k_work *work)
{ return reinterpret_cast<k_work_delayable *>(work); }
std::uint32_t k_uptime_get_32() { return static_cast<std::uint32_t>(now); }
bool gpio_is_ready_dt(const gpio_dt_spec *) { return controller_ready; }
int gpio_pin_configure_dt(const gpio_dt_spec *, unsigned flags)
{ CHECK(flags == GPIO_INPUT || flags == GPIO_OUTPUT_INACTIVE); return configure_error; }
void gpio_init_callback(gpio_callback *cb, gpio_callback_handler_t handler, gpio_port_pins_t pins)
{ cb->handler = handler; cb->pins = pins; }
int gpio_add_callback(const device *, gpio_callback *cb)
{
    if (add_error) return add_error;
    for (unsigned pin_number = 0; pin_number < callbacks.size(); ++pin_number)
        if (cb->pins & (1U << pin_number)) callbacks[pin_number] = cb;
    return 0;
}
int gpio_remove_callback(const device *, gpio_callback *cb)
{ for (auto &entry : callbacks) if (entry == cb) entry = nullptr; return 0; }
int gpio_pin_interrupt_configure_dt(const gpio_dt_spec *, unsigned flags)
{ CHECK(flags == GPIO_INT_EDGE_BOTH); return interrupt_error; }
int gpio_pin_get_dt(const gpio_dt_spec *input)
{
    CHECK(!in_isr); ++reads;
    const int snapshot = levels[input->pin];
    if (read_hook) { auto hook = std::move(read_hook); read_hook = {}; hook(); }
    return snapshot;
}

int gpio_pin_set_dt(const gpio_dt_spec *, int value)
{ led_writes.push_back({now,value}); return write_error; }

int main()
{
    using pico::EventKind;
    using pico::platform::GpioButton;
    reset();
    GpioButton focus;
    CHECK(focus.init(pin, sink, EventKind::FocusButtonTapped, EventKind::FocusButtonHeld));
    advance(10000);
    CHECK(reads == 1 && events.empty()); // boot sample, then no idle reads
    edge(2, 1); advance(10010);
    edge(2, 0); advance(10015);
    edge(2, 1); advance(10045); // stable press after bounce
    edge(2, 0); advance(10055);
    edge(2, 1); advance(10060);
    edge(2, 0); advance(10090);
    CHECK(events.size() == 1 && events[0] == EventKind::FocusButtonTapped);
    const auto idle_reads = reads;
    advance(20000);
    CHECK(reads == idle_reads);
    edge(2, 1); advance(21030);
    CHECK(events.size() == 2 && events.back() == EventKind::FocusButtonHeld);
    const auto held_reads = reads;
    advance(30000); CHECK(reads == held_reads); // no repeats while held
    edge(2, 0); advance(30030); CHECK(events.size() == 2);

    reset(UINT32_MAX - 20ULL);
    GpioButton boot_held;
    levels[2] = 1;
    CHECK(boot_held.init(pin, sink, EventKind::NavigateNext, EventKind::Back));
    advance(now + 1030);
    CHECK(events.size() == 1 && events.back() == EventKind::Back);
    edge(2, 0); advance(now + 30); CHECK(events.size() == 1);

    reset();
    GpioButton racing;
    CHECK(racing.init(pin, sink, EventKind::NavigateNext, EventKind::Back));
    advance(0); edge(2, 1); advance(0);
    // A release ISR arrives inside the debounce worker, after its GPIO snapshot.
    read_hook = [] { edge(2, 0); };
    advance(60);
    CHECK(events.size() == 1 && events.back() == EventKind::NavigateNext);
    advance(2000); CHECK(events.size() == 1); // queued edge was not overwritten

    reset();
    GpioButton failed_read;
    CHECK(failed_read.init(pin, sink, EventKind::Back, EventKind::Back));
    advance(0); edge(2, 1); advance(30);
    edge(2, -5); advance(2000);
    CHECK(events.empty());
    edge(2, 0); advance(2030); CHECK(events.empty());
    edge(2, 1); advance(2060); edge(2, 0); advance(2090);
    CHECK(events.size() == 1 && events.back() == EventKind::Back);

    reset();
    GpioButton action, nav, back;
    CHECK(action.init(pin, sink, EventKind::FocusButtonTapped, EventKind::FocusButtonHeld));
    CHECK(nav.init({&port, 3, 0}, sink, EventKind::NavigateNext, EventKind::Back));
    CHECK(back.init({&port, 4, 0}, sink, EventKind::Back, EventKind::Back));
    advance(0);
    edge(2, 1); edge(3, 1); edge(4, 1); advance(30);
    edge(2, 0); edge(3, 0); edge(4, 0); advance(60);
    CHECK(events.size() == 3);
    CHECK(events[0] == EventKind::FocusButtonTapped);
    CHECK(events[1] == EventKind::NavigateNext && events[2] == EventKind::Back);

    for (unsigned failure = 0; failure < 4; ++failure) {
        reset(); GpioButton absent;
        if (failure == 0) controller_ready = false;
        if (failure == 1) configure_error = -5;
        if (failure == 2) add_error = -5;
        if (failure == 3) interrupt_error = -5;
        CHECK(!absent.init(pin, sink, EventKind::Back, EventKind::Back));
        CHECK(callbacks[2] == nullptr);
        for (const auto *job : jobs) CHECK(!job->pending);
    }
    reset();
    pico::platform::StatusLed heartbeat;
    CHECK(heartbeat.init({&port,6,0}));
    advance(0); CHECK(led_writes.size()==1 && led_writes.back().second==1);
    advance(199); CHECK(led_writes.size()==1);
    advance(200); CHECK(led_writes.back() == std::make_pair(std::uint64_t{200},0));
    advance(5199); CHECK(led_writes.size()==2);
    advance(5200); CHECK(led_writes.back() == std::make_pair(std::uint64_t{5200},1));
    advance(5400); CHECK(led_writes.back().second==0);
    CHECK(pico::status_led::on(199) && !pico::status_led::on(200));
    CHECK(!pico::status_led::on(5199) && pico::status_led::on(5200));
    write_error=-5; advance(10400);
    const auto failed_writes=led_writes.size(); advance(20000);
    CHECK(led_writes.size()==failed_writes); // failed GPIO stops, no retry loop
    if (failures) return EXIT_FAILURE;
    std::cout << "GPIO button adapter model tests passed\n";
}
