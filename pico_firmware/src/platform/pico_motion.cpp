#include "pico_motion.hpp"
#include <pico/motion.hpp>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pico_motion,CONFIG_PICO_LOG_LEVEL);
#if defined(CONFIG_PICO_MOTION) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_imu), okay)
#include <zephyr/drivers/sensor.h>
namespace {
const device *imu = DEVICE_DT_GET(DT_ALIAS(pico_imu));
pico::platform::EventSink event_sink;
pico::motion::Detector detector;
bool sensor_ready;
std::uint8_t last_config = 0;
std::uint32_t last_poll = 0;
unsigned errors = 0;
std::int32_t milli_g(const sensor_value &value)
{
    const auto micro_ms2 = static_cast<std::int64_t>(value.val1)*1000000 + value.val2;
    return static_cast<std::int32_t>(micro_ms2*1000/9806650);
}
int rate(int hz)
{
    const sensor_value value{hz,0};
    return sensor_attr_set(imu,SENSOR_CHAN_ACCEL_XYZ,SENSOR_ATTR_SAMPLING_FREQUENCY,&value);
}
}
#endif
namespace pico::platform {
bool Motion::init(EventSink sink)
{
#if defined(CONFIG_PICO_MOTION) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_imu), okay)
    event_sink = sink;
    if (!device_is_ready(imu)) { LOG_WRN("Sense IMU not ready; motion reactions disabled"); return false; }
    const sensor_value off{0,0};
    int error = sensor_attr_set(imu,SENSOR_CHAN_GYRO_XYZ,SENSOR_ATTR_SAMPLING_FREQUENCY,&off);
    if (!error) error = rate(0);
    if (error) { LOG_WRN("IMU configuration failed (%d)",error); return false; }
    sensor_ready = true;
    LOG_INF("Sense motion ready; accelerometer 26 Hz on demand, gyro off");
    return true;
#else
    ARG_UNUSED(sink);
    LOG_INF("Motion adapter disabled; buttons remain available");
    return false;
#endif
}
bool Motion::ready() const
{
#if defined(CONFIG_PICO_MOTION) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_imu), okay)
    return sensor_ready;
#else
    return false;
#endif
}
std::uint32_t Motion::next_poll_ms(std::uint8_t config, std::uint32_t now) const
{
#if defined(CONFIG_PICO_MOTION) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_imu), okay)
    if (!sensor_ready) return UINT32_MAX;
    if (config != last_config) return 0;
    if (!config) return UINT32_MAX;
    const auto elapsed = now - last_poll;
    return elapsed >= 40U ? 0U : 40U - elapsed;
#else
    ARG_UNUSED(config); ARG_UNUSED(now);
    return UINT32_MAX;
#endif
}
void Motion::poll(std::uint8_t config)
{
#if defined(CONFIG_PICO_MOTION) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_imu), okay)
    if (!sensor_ready) return;
    if (config != last_config) {
        const int error = rate(config ? 26 : 0);
        if (error) { sensor_ready = false; LOG_WRN("IMU rate change failed (%d)",error); return; }
        last_config = config; detector.reset(); errors = 0; last_poll = k_uptime_get_32();
    }
    const auto now = k_uptime_get_32();
    if (!config || now-last_poll < 40U) return;
    last_poll = now;
    sensor_value axes[3];
    int error = sensor_sample_fetch_chan(imu,SENSOR_CHAN_ACCEL_XYZ);
    if (!error) error = sensor_channel_get(imu,SENSOR_CHAN_ACCEL_XYZ,axes);
    if (error) {
        detector.reset();
        if (errors == 0 && event_sink) event_sink({EventKind::MotionActive,now});
        if (++errors >= 3) {
            rate(0); sensor_ready = false;
            LOG_WRN("IMU read failed three times (%d); motion disabled until restart",error);
        }
        return;
    }
    errors = 0;
    const motion::Sample sample{milli_g(axes[0]),milli_g(axes[1]),milli_g(axes[2])};
    const auto signals = detector.sample(sample,now);
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
    if (signals) LOG_INF("Motion signals=0x%02x mg=%d,%d,%d",static_cast<unsigned>(signals),
                        static_cast<int>(sample.x),static_cast<int>(sample.y),static_cast<int>(sample.z));
#endif
    if (!event_sink) return;
    const struct { motion::Signal signal; EventKind event; } mapping[]{
        {motion::Settled,EventKind::MotionSettled},{motion::Active,EventKind::MotionActive},
        {motion::Pickup,EventKind::MotionPickup},{motion::Rock,EventKind::MotionRock},{motion::Rest,EventKind::MotionRest}};
    for (const auto &entry:mapping)
        if (signals & entry.signal) event_sink({entry.event,now});
#else
    ARG_UNUSED(config);
#endif
}
}
