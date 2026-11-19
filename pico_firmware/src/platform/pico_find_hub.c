/* Optional measurement adapter, NCS 3.4.2. No live enrollment entry point. */
#include "pico_find_hub.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <bluetooth/fast_pair/fast_pair.h>
#include <bluetooth/fast_pair/adv_manager.h>
#include <bluetooth/fast_pair/fhn/fhn.h>

LOG_MODULE_REGISTER(pico_fhn, CONFIG_PICO_LOG_LEVEL);
BUILD_ASSERT(FIXED_PARTITION_OFFSET(bt_fast_pair_partition) == 0xeb000);
BUILD_ASSERT(FIXED_PARTITION_SIZE(bt_fast_pair_partition) == 0x1000);
BUILD_ASSERT(FIXED_PARTITION_OFFSET(storage_partition) == 0xec000);
BUILD_ASSERT(FIXED_PARTITION_SIZE(storage_partition) == 0x8000);
BUILD_ASSERT(CONFIG_BT_MAX_CONN >= CONFIG_BT_FAST_PAIR_FHN_MAX_CONN + 2);

int pico_find_hub_measurement_init(void)
{
    const struct device *flash = FIXED_PARTITION_DEVICE(bt_fast_pair_partition);
    struct flash_pages_info page;
    if (!device_is_ready(flash)) return -ENODEV;
    int err = flash_get_page_info_by_offs(flash,
        FIXED_PARTITION_OFFSET(bt_fast_pair_partition), &page);
    if (err) return err;
    if (page.start_offset != 0xeb000 || page.size != 0x1000) return -EINVAL;
    LOG_INF("Combined measurement profile; network activation disabled; credential page verified");
    return 0;
}

/* Fail closed: this milestone has no acoustic or DULT motion qualification.
 * Never acknowledge sound that has not been produced. No mock motion silence. */
static void ring_failed(enum bt_fast_pair_fhn_ring_src src)
{
    const struct bt_fast_pair_fhn_ring_state_param state = {
        .trigger = BT_FAST_PAIR_FHN_RING_TRIGGER_FAILED,
        .active_comp_bm = BT_FAST_PAIR_FHN_RING_COMP_BM_NONE,
    };
    (void)bt_fast_pair_fhn_ring_state_update(src, &state);
}
static void ring_start(enum bt_fast_pair_fhn_ring_src src,
                       const struct bt_fast_pair_fhn_ring_req_param *param)
{
    ARG_UNUSED(param);
    ring_failed(src);
}
static const struct bt_fast_pair_fhn_ring_cb ring_cb = {
    .start_request = ring_start, .timeout_expired = ring_failed, .stop_request = ring_failed,
};
static void motion_unavailable(void) {}
static bool motion_fail_safe(void) { return true; }
static const struct bt_fast_pair_fhn_motion_detector_cb motion_cb = {
    .start = motion_unavailable, .period_expired = motion_fail_safe, .stop = motion_unavailable,
};
/* SDK user-reset hook: never erase identity 0, pico/state, or all settings. */
int bt_fast_pair_factory_reset_user_action_perform(void)
{
    return bt_id_reset(1, NULL, NULL) < 0 ? -EIO : 0;
}

/* Size anchor deliberately retains the SDK activation/teardown/reset call graph.
 * There is NO runtime caller: activation remains a separate, reviewed milestone.
 * Run any future caller only on the cooperative system workqueue after bt_enable
 * and settings_load. GPIO/mic/display/audio/Core continue to have one owner.
 * This is a sizing probe, NOT an approved activation implementation. */
static int activation_size_probe(bool enable)
{
    int err;
    const struct bt_fast_pair_fhn_adv_param adv = BT_FAST_PAIR_FHN_ADV_PARAM_INIT(0xc80, 0xc80);
    if (!enable) {
        err = bt_fast_pair_adv_manager_disable();
        if (err) return err;
        err = bt_fast_pair_disable();
        if (err) return err;
        return bt_fast_pair_factory_reset();
    }
    if ((err = bt_fast_pair_adv_manager_id_set(1)) ||
        (err = bt_fast_pair_fhn_id_set(1)) ||
        (err = bt_fast_pair_fhn_adv_param_set(&adv)) ||
        (err = bt_fast_pair_fhn_ring_cb_register(&ring_cb)) ||
        (err = bt_fast_pair_fhn_motion_detector_cb_register(&motion_cb)) ||
        (err = bt_fast_pair_fhn_battery_level_set(BT_FAST_PAIR_FHN_BATTERY_LEVEL_NONE)) ||
        (err = bt_fast_pair_enable())) return err;
    return bt_fast_pair_adv_manager_enable();
}
__attribute__((used, section(".pico_fhn_size_probe")))
static int (* const retained_activation_probe)(bool) = activation_size_probe;
/* Keep named API entry points visible under LTO for machine-checkable reports. */
__attribute__((used, section(".pico_fhn_size_probe")))
static int (* const retained_sdk_enable)(void) = bt_fast_pair_enable;
__attribute__((used, section(".pico_fhn_size_probe")))
static int (* const retained_sdk_adv_enable)(void) = bt_fast_pair_adv_manager_enable;
__attribute__((used, section(".pico_fhn_size_probe")))
static int (* const retained_sdk_ring_update)(enum bt_fast_pair_fhn_ring_src,
    const struct bt_fast_pair_fhn_ring_state_param *) = bt_fast_pair_fhn_ring_state_update;
