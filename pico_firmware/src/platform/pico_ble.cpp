#include "pico_ble.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <pico/protocol.hpp>

LOG_MODULE_REGISTER(pico_ble, CONFIG_PICO_LOG_LEVEL);

#define BT_UUID_PICO_SERVICE_VAL                                                \
    BT_UUID_128_ENCODE(0x8e7b0001, 0x54f2, 0x4d9b, 0xa3d1, 0x9f150000c001)
#define BT_UUID_PICO_STATUS_VAL                                                 \
    BT_UUID_128_ENCODE(0x8e7b0002, 0x54f2, 0x4d9b, 0xa3d1, 0x9f150000c001)
#define BT_UUID_PICO_COMMAND_VAL                                                \
    BT_UUID_128_ENCODE(0x8e7b0003, 0x54f2, 0x4d9b, 0xa3d1, 0x9f150000c001)

#define BT_UUID_PICO_LOCATOR_VAL \
    BT_UUID_128_ENCODE(0x8e7b0004, 0x54f2, 0x4d9b, 0xa3d1, 0x9f150000c001)

namespace {

#define BT_UUID_PICO_SERVICE BT_UUID_DECLARE_128(BT_UUID_PICO_SERVICE_VAL)
#define BT_UUID_PICO_STATUS BT_UUID_DECLARE_128(BT_UUID_PICO_STATUS_VAL)
#define BT_UUID_PICO_COMMAND BT_UUID_DECLARE_128(BT_UUID_PICO_COMMAND_VAL)

pico::platform::EventSink event_sink;
std::array<std::uint8_t, pico::protocol::kStatusSize> status_value{};
K_MUTEX_DEFINE(status_mutex);
std::array<std::uint8_t,4> locator_value{{1,0,0,0}};

ssize_t read_locator(bt_conn *connection, const bt_gatt_attr *attribute,
                     void *buffer, std::uint16_t length, std::uint16_t offset)
{
    std::array<std::uint8_t,4> snapshot{};
    k_mutex_lock(&status_mutex,K_FOREVER); snapshot=locator_value; k_mutex_unlock(&status_mutex);
    return bt_gatt_attr_read(connection,attribute,buffer,length,offset,snapshot.data(),snapshot.size());
}

bool emit(pico::EventKind kind, std::uint32_t value = 0)
{
    return event_sink != nullptr && event_sink({kind, value});
}

ssize_t read_status(bt_conn *connection, const bt_gatt_attr *attribute,
                    void *buffer, std::uint16_t length, std::uint16_t offset)
{
    std::array<std::uint8_t, pico::protocol::kStatusSize> snapshot{};
    k_mutex_lock(&status_mutex, K_FOREVER);
    snapshot = status_value;
    k_mutex_unlock(&status_mutex);
    return bt_gatt_attr_read(connection, attribute, buffer, length, offset,
                             snapshot.data(), snapshot.size());
}

ssize_t write_command(bt_conn *, const bt_gatt_attr *, const void *buffer,
                      std::uint16_t length, std::uint16_t offset, std::uint8_t)
{
    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    const auto decoded = pico::protocol::decode_command(
        static_cast<const std::uint8_t *>(buffer), length);
    if (decoded.status != pico::protocol::DecodeStatus::Ok) {
        LOG_WRN("Rejected BLE command (%u)",
                static_cast<unsigned int>(decoded.status));
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    if (!emit(decoded.event.kind, decoded.event.value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }
    return length;
}

void status_subscription_changed(const bt_gatt_attr *, std::uint16_t value)
{
    LOG_INF("Status notifications %s",
            value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(
    pico_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_PICO_SERVICE),
    BT_GATT_CHARACTERISTIC(BT_UUID_PICO_STATUS,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_status, nullptr, nullptr),
    BT_GATT_CCC(status_subscription_changed,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_CHARACTERISTIC(BT_UUID_PICO_COMMAND,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT, nullptr, write_command,
                           nullptr),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_PICO_LOCATOR_VAL),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,read_locator,nullptr,nullptr),
    BT_GATT_CCC(status_subscription_changed,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));

const bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_PICO_SERVICE_VAL),
};

const bt_data scan_response_data[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

int begin_advertising()
{
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, advertising_data,
                           ARRAY_SIZE(advertising_data), scan_response_data,
                           ARRAY_SIZE(scan_response_data));
}

void connected(bt_conn *connection, std::uint8_t error)
{
    if (error != 0U) {
        LOG_WRN("BLE connection failed (0x%02x)", error);
        return;
    }

    LOG_INF("BLE central connected");
    emit(pico::EventKind::Connected);
#if defined(CONFIG_PICO_BLE_AUTO_SECURITY)
    const int security_error = bt_conn_set_security(connection, BT_SECURITY_L2);
    if (security_error != 0 && security_error != -EALREADY) {
        LOG_WRN("Could not request encrypted link (%d)", security_error);
    }
#else
    ARG_UNUSED(connection);
    LOG_INF("Waiting for phone-initiated pairing; use Bond in nRF Connect");
#endif
}

void disconnected(bt_conn *, std::uint8_t reason)
{
    LOG_INF("BLE central disconnected (0x%02x)", reason);
    emit(pico::EventKind::Disconnected);
}

void connection_recycled()
{
    const int error = begin_advertising();
    if (error != 0) {
        LOG_WRN("Could not resume advertising (%d)", error);
    }
}

void security_changed(bt_conn *, bt_security_t level, enum bt_security_err error)
{
    if (error != BT_SECURITY_ERR_SUCCESS) {
        LOG_WRN("BLE security failed (%u)", static_cast<unsigned int>(error));
        return;
    }

    LOG_INF("BLE link security level %u", static_cast<unsigned int>(level));
    emit(pico::EventKind::RequestStatus);
}

const char *security_error_name(bt_security_err error)
{
    switch (error) {
    case BT_SECURITY_ERR_SUCCESS: return "success";
    case BT_SECURITY_ERR_AUTH_FAIL: return "authentication failed";
    case BT_SECURITY_ERR_PIN_OR_KEY_MISSING: return "PIN or key missing";
    case BT_SECURITY_ERR_OOB_NOT_AVAILABLE: return "OOB not available";
    case BT_SECURITY_ERR_AUTH_REQUIREMENT: return "security requirements mismatch";
    case BT_SECURITY_ERR_PAIR_NOT_SUPPORTED: return "pairing not supported";
    case BT_SECURITY_ERR_PAIR_NOT_ALLOWED: return "pairing not allowed";
    case BT_SECURITY_ERR_INVALID_PARAM: return "invalid parameter";
    case BT_SECURITY_ERR_KEY_REJECTED: return "key rejected";
    case BT_SECURITY_ERR_UNSPECIFIED: return "unspecified (check SMP log)";
    default: return "unknown";
    }
}

void pairing_complete(bt_conn *, bool bonded)
{
    LOG_INF("BLE pairing complete; bonded=%u", static_cast<unsigned int>(bonded));
}

void pairing_failed(bt_conn *, enum bt_security_err reason)
{
    LOG_WRN("BLE pairing failed: %s (%u)", security_error_name(reason),
            static_cast<unsigned int>(reason));
}

bt_conn_auth_info_cb pairing_callbacks = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = connection_recycled,
    .security_changed = security_changed,
};

} // namespace

namespace pico::platform {

bool Ble::init(EventSink sink)
{
    event_sink = sink;
    const int callback_error = bt_conn_auth_info_cb_register(&pairing_callbacks);
    if (callback_error != 0) {
        LOG_ERR("Could not register pairing diagnostics (%d)", callback_error);
        return false;
    }
    const int error = bt_enable(nullptr);
    if (error != 0) {
        bt_conn_auth_info_cb_unregister(&pairing_callbacks);
        LOG_ERR("Bluetooth initialization failed (%d)", error);
        return false;
    }
    ready_ = true;
    LOG_INF("Bluetooth initialized");
    return true;
}

bool Ble::start_advertising()
{
    if (!ready_) {
        return false;
    }
    const int error = begin_advertising();
    if (error != 0) {
        LOG_ERR("Advertising start failed (%d)", error);
        return false;
    }
    LOG_INF("Advertising as %s", CONFIG_BT_DEVICE_NAME);
    return true;
}

void Ble::publish(const State &state, Capabilities capabilities)
{
    if (!ready_) {
        return;
    }
    std::uint8_t encoded[protocol::kStatusSize]{};
    protocol::encode_status(state, capabilities, encoded);

    std::uint8_t locator[protocol::kLocatorStatusSize]{};
    protocol::encode_locator(state,capabilities,locator);
    k_mutex_lock(&status_mutex, K_FOREVER);
    std::memcpy(locator_value.data(),locator,sizeof(locator));
    std::memcpy(status_value.data(), encoded, sizeof(encoded));
    k_mutex_unlock(&status_mutex);

    bt_gatt_notify(nullptr,&pico_service.attrs[7],locator,sizeof(locator));
    const int error = bt_gatt_notify(nullptr, &pico_service.attrs[2], encoded,
                                     sizeof(encoded));
    if (error != 0 && error != -ENOTCONN) {
        LOG_DBG("Status notification not sent (%d)", error);
    }
}

} // namespace pico::platform
