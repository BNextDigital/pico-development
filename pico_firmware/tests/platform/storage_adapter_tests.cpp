#include <cassert>
#include <cstring>
#include <cerrno>
#define CONFIG_PICO_SETTINGS 1
#define CONFIG_PICO_FIND_HUB 1
#define CONFIG_PICO_SETTINGS_SAVE_DELAY_SECONDS 5
#include "platform/pico_storage.cpp"
namespace {
pico::persistence::Blob disk{};
std::size_t disk_size;
pico::clock::Blob clock_disk{};
std::size_t clock_disk_size;
int global_error;
int write_error;
bool inject_newer;
pico::platform::Storage storage;
pico::PersistedState newer{};
ssize_t read_disk(void *,void *out,std::size_t size) {
    const auto bytes = disk_size < size ? disk_size : size;
    std::memcpy(out,disk.data(),bytes); return bytes;
}
}
ssize_t read_clock_disk(void *,void *out,std::size_t size) {
    const auto bytes=clock_disk_size<size ? clock_disk_size : size;
    std::memcpy(out,clock_disk.data(),bytes); return bytes;
}
int settings_subsys_init() { return 0; }
int settings_load() {
    settings_set("state",disk.size(),read_disk,nullptr);
    clock_settings_set("config",clock_disk.size(),read_clock_disk,nullptr);
    return global_error;
}
int settings_save_one(const char *name,const void *data,std::size_t size) {
    if (!std::strcmp(name,"pico_clock/config")) {
        if (write_error) return write_error;
        std::memcpy(clock_disk.data(),data,size); clock_disk_size=size; return 0;
    }
    assert(std::strcmp(name,"pico/state") == 0);
    if (inject_newer) { inject_newer=false; storage.save(newer); }
    if (write_error) return write_error;
    std::memcpy(disk.data(),data,size); disk_size=size; return 0;
}
int main() {
    assert(storage.init());
    pico::PersistedState initial{}; initial.interaction_count=1;
    disk=pico::persistence::encode(initial); disk_size=disk.size();
    pico::PersistedState restored{};
    global_error=-EIO; // FP namespace failure must not discard Pico data.
    assert(storage.load(restored)); assert(restored.interaction_count==1);
    global_error=0;
    disk_size=7; assert(!storage.load(restored)); // Interrupted/truncated read.
    disk_size=disk.size(); disk[5]^=1; assert(!storage.load(restored));
    disk=pico::persistence::encode(initial);
    newer=initial; newer.interaction_count=2;
    storage.save(initial); write_error=-ENOMEM;
    flush_pending_state(nullptr); // Low-memory/failed write retains retry snapshot.
    assert(pending_blob_dirty); assert(flush_state_work.delay==30);
    inject_newer=true;
    flush_pending_state(nullptr); // New snapshot arriving during failed write wins.
    assert(pending_blob==pico::persistence::encode(newer));
    write_error=0; flush_pending_state(nullptr);
    assert(!pending_blob_dirty);
    loaded_blob_present=false; // Simulated restart: only durable snapshot restored.
    assert(storage.load(restored)); assert(restored.interaction_count==2);
    pico::clock::Settings settings; settings.hour24=false; settings.alarms[0]={420,31,true,-1};
    storage.save_clock(settings); write_error=-EIO;
    flush_pending_state(nullptr); assert(pending_clock_dirty);
    settings.usb_desk=true; storage.save_clock(settings);
    write_error=0; flush_pending_state(nullptr); assert(!pending_clock_dirty);
    storage.load(restored);
    pico::clock::Settings clock_restored;
    assert(storage.load_clock(clock_restored));
    assert(!clock_restored.hour24 && clock_restored.usb_desk && clock_restored.alarms[0].enabled);
    clock_disk_size=8; storage.load(restored); assert(!storage.load_clock(clock_restored));
    assert(restored.interaction_count==2); // A corrupt clock record never clears personality.
}
