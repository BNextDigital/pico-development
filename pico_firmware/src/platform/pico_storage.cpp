#include "pico_storage.hpp"

#include <cerrno>
#include <cstring>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <pico/persistence.hpp>

LOG_MODULE_REGISTER(pico_storage, CONFIG_PICO_LOG_LEVEL);

namespace {

pico::persistence::Blob loaded_blob{};
pico::persistence::Blob pending_blob{};
bool loaded_blob_present;
std::size_t loaded_blob_size;
bool pending_blob_dirty;
pico::clock::Blob loaded_clock_blob{}, pending_clock_blob{};
bool loaded_clock_present, pending_clock_dirty;
std::uint32_t pending_generation;
K_MUTEX_DEFINE(pending_blob_mutex);

#if defined(CONFIG_PICO_SETTINGS)
void flush_pending_state(k_work *);
K_WORK_DELAYABLE_DEFINE(flush_state_work, flush_pending_state);

template<typename Blob>
bool flush_one(const char *key, Blob &pending, bool &dirty)
{
    Blob snapshot{};
    k_mutex_lock(&pending_blob_mutex, K_FOREVER);
    if (!dirty) { k_mutex_unlock(&pending_blob_mutex); return false; }
    snapshot=pending; dirty=false;
    k_mutex_unlock(&pending_blob_mutex);
    const int error=settings_save_one(key,snapshot.data(),snapshot.size());
    if (!error) return false;
    LOG_WRN("Could not flush %s (%d); retrying",key,error);
    bool retry=false;
    k_mutex_lock(&pending_blob_mutex,K_FOREVER);
    if (!dirty) { pending=snapshot; dirty=true; retry=true; }
    k_mutex_unlock(&pending_blob_mutex);
    return retry;
}
void flush_pending_state(k_work *)
{
    k_mutex_lock(&pending_blob_mutex,K_FOREVER);
    const auto generation=pending_generation;
    k_mutex_unlock(&pending_blob_mutex);
    const bool retry_state=flush_one("pico/state",pending_blob,pending_blob_dirty);
    const bool retry_clock=flush_one("pico_clock/config",pending_clock_blob,pending_clock_dirty);
    k_mutex_lock(&pending_blob_mutex,K_FOREVER);
    const bool newer=pending_generation!=generation;
    k_mutex_unlock(&pending_blob_mutex);
    // A newer save already scheduled its short coalescing delay. Do not replace
    // that deadline with an old failed snapshot's longer retry backoff.
    if ((retry_state || retry_clock) && !newer) k_work_reschedule(&flush_state_work,K_SECONDS(30));
}
int clock_settings_set(const char *name,std::size_t length,settings_read_cb read_cb,void *argument)
{
    if (std::strcmp(name,"config")) return -ENOENT;
    loaded_clock_present=false;
    if (length!=loaded_clock_blob.size()) return 0;
    const int bytes=read_cb(argument,loaded_clock_blob.data(),length);
    if (bytes<0) return bytes;
    loaded_clock_present=static_cast<std::size_t>(bytes)==length;
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(pico_clock_config,"pico_clock",nullptr,clock_settings_set,nullptr,nullptr);

int settings_set(const char *name, std::size_t length, settings_read_cb read_cb,
                 void *callback_argument)
{
    if (std::strcmp(name, "state") != 0) {
        return -ENOENT;
    }
    if (length != 16U && length != 20U && length != 28U && length != loaded_blob.size()) {
        LOG_WRN("Ignoring stored state with unexpected size %u",
                static_cast<unsigned int>(length));
        return 0;
    }

    const int bytes_read = read_cb(callback_argument, loaded_blob.data(), length);
    if (bytes_read < 0) {
        return bytes_read;
    }
    loaded_blob_size = static_cast<std::size_t>(bytes_read);
    loaded_blob_present = loaded_blob_size == length;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pico_state, "pico", nullptr, settings_set,
                               nullptr, nullptr);
#endif

} // namespace

namespace pico::platform {

bool Storage::init()
{
#if defined(CONFIG_PICO_SETTINGS)
    const int error = settings_subsys_init();
    if (error != 0) {
        LOG_ERR("Settings subsystem initialization failed (%d)", error);
        return false;
    }
    ready_ = true;
    return true;
#else
    LOG_WRN("Persistent state is disabled");
    return false;
#endif
}

bool Storage::load(PersistedState &state)
{
#if defined(CONFIG_PICO_SETTINGS)
    if (!ready_) {
        return false;
    }
    loaded_blob_present = false;
    loaded_clock_present = false;
    const int error = settings_load();
    if (error != 0) {
        LOG_ERR("Settings load failed (%d)", error);
#if !defined(CONFIG_PICO_FIND_HUB)
        return false;
#endif
        // A separate namespace failing must not discard a valid Pico snapshot.
        // Decode below still rejects absent, truncated or corrupt Pico data.
    }
    if (!loaded_blob_present) {
        LOG_INF("No saved Pico state; using defaults");
        return false;
    }
    if (!persistence::decode(loaded_blob.data(), loaded_blob_size, state)) {
        LOG_WRN("Saved Pico state failed validation; using defaults");
        return false;
    }
    LOG_INF("Restored Pico state");
    return true;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

bool Storage::save(const PersistedState &state)
{
#if defined(CONFIG_PICO_SETTINGS)
    if (!ready_) {
        return false;
    }
    k_mutex_lock(&pending_blob_mutex, K_FOREVER);
    pending_blob = persistence::encode(state);
    pending_blob_dirty = true;
    ++pending_generation;
    k_mutex_unlock(&pending_blob_mutex);
    k_work_reschedule(&flush_state_work,
                      K_SECONDS(CONFIG_PICO_SETTINGS_SAVE_DELAY_SECONDS));
    return true;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

bool Storage::load_clock(clock::Settings &settings)
{
#if defined(CONFIG_PICO_SETTINGS)
    return ready_ && loaded_clock_present && clock::decode(loaded_clock_blob.data(),loaded_clock_blob.size(),settings);
#else
    ARG_UNUSED(settings); return false;
#endif
}
bool Storage::save_clock(const clock::Settings &settings)
{
#if defined(CONFIG_PICO_SETTINGS)
    if (!ready_ || !clock::valid_settings(settings)) return false;
    k_mutex_lock(&pending_blob_mutex,K_FOREVER);
    pending_clock_blob=clock::encode(settings); pending_clock_dirty=true; ++pending_generation;
    k_mutex_unlock(&pending_blob_mutex);
    k_work_reschedule(&flush_state_work,K_SECONDS(CONFIG_PICO_SETTINGS_SAVE_DELAY_SECONDS));
    return true;
#else
    ARG_UNUSED(settings); return false;
#endif
}

} // namespace pico::platform
