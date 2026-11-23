#include "pico_ble.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <pico/protocol.hpp>
#if defined(CONFIG_PICO_FIND_HUB)
#include <pico/tracking_policy.hpp>
#endif

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
std::array<std::uint8_t,pico::protocol::kClockStatusSize> clock_value{};
pico::clock::Blob clock_config_value{};
std::uint32_t clock_uptime, stopwatch_uptime_ms;
constexpr std::uint8_t clock_attr_index=10;
std::uint32_t read32(const std::uint8_t *p)
{ return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
void write32(std::uint8_t *p,std::uint32_t value)
{ for (unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(value>>(8*i)); }
ssize_t read_clock(bt_conn *conn,const bt_gatt_attr *attr,void *buffer,std::uint16_t length,std::uint16_t offset)
{
    std::array<std::uint8_t,pico::protocol::kClockStatusSize> value{};
    k_mutex_lock(&status_mutex,K_FOREVER);
    value=clock_value;
    const auto elapsed=static_cast<std::uint32_t>(k_uptime_get()/1000U)-clock_uptime;
    const auto stopwatch_elapsed=k_uptime_get_32()-stopwatch_uptime_ms;
    k_mutex_unlock(&status_mutex);
    if (value[1]&1U) {
        const auto utc=static_cast<std::uint64_t>(read32(value.data()+2))+elapsed;
        const auto raw=value[6]|(unsigned(value[7])<<8);
        const int zone=raw>=32768U ? static_cast<int>(raw)-65536 : static_cast<int>(raw);
        if (utc<=pico::clock::kMaxUtc && pico::clock::valid_time(static_cast<std::uint32_t>(utc),zone))
            write32(value.data()+2,static_cast<std::uint32_t>(utc));
        else { value[1]=static_cast<std::uint8_t>((value[1]&~1U)|2U); write32(value.data()+2,0); }
        const auto age=std::min<std::uint64_t>(UINT32_MAX,static_cast<std::uint64_t>(read32(value.data()+14))+elapsed);
        write32(value.data()+14,static_cast<std::uint32_t>(age));
        if (age>=86400U) value[1]|=2U;
    }
    if (value[1]&64U) {
        const auto milliseconds=static_cast<std::uint64_t>(stopwatch_elapsed)+value[18]+(unsigned(value[19])<<8);
        write32(value.data()+8,static_cast<std::uint32_t>(std::min<std::uint64_t>(UINT32_MAX,
            static_cast<std::uint64_t>(read32(value.data()+8))+milliseconds/1000U)));
        value[18]=static_cast<std::uint8_t>(milliseconds%1000U);
        value[19]=static_cast<std::uint8_t>((milliseconds%1000U)>>8);
    }
    return bt_gatt_attr_read(conn,attr,buffer,length,offset,value.data(),value.size());
}
ssize_t read_clock_config(bt_conn *conn,const bt_gatt_attr *attr,void *buffer,std::uint16_t length,std::uint16_t offset)
{
    pico::clock::Blob value{};
    k_mutex_lock(&status_mutex,K_FOREVER); value=clock_config_value; k_mutex_unlock(&status_mutex);
    return bt_gatt_attr_read(conn,attr,buffer,length,offset,value.data(),value.size());
}


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
    if (event_sink == nullptr || !event_sink(decoded.event)) {
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
                           BT_GATT_PERM_READ_AUTHEN, read_status, nullptr, nullptr),
    BT_GATT_CCC(status_subscription_changed,
                BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
    BT_GATT_CHARACTERISTIC(BT_UUID_PICO_COMMAND,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_AUTHEN, nullptr, write_command,
                           nullptr),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_PICO_LOCATOR_VAL),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_AUTHEN,read_locator,nullptr,nullptr),
    BT_GATT_CCC(status_subscription_changed,
                BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x8e7b0005,0x54f2,0x4d9b,0xa3d1,0x9f150000c001)),
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,BT_GATT_PERM_READ_AUTHEN,read_clock,nullptr,nullptr),
    BT_GATT_CCC(status_subscription_changed,BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_WRITE_AUTHEN),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x8e7b0006,0x54f2,0x4d9b,0xa3d1,0x9f150000c001)),
        BT_GATT_CHRC_READ,BT_GATT_PERM_READ_AUTHEN,read_clock_config,nullptr,nullptr));

const bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_PICO_SERVICE_VAL),
};

const bt_data scan_response_data[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

bool is_companion(bt_conn *conn)
{
    bt_conn_info info{};
    return bt_conn_get_info(conn, &info) == 0 && info.id==BT_ID_DEFAULT;
}
#if defined(CONFIG_PICO_FIND_HUB)
struct ConnectionCounts { unsigned companion{}, total{}; };
void count_connections(bt_conn *conn, void *data)
{
    bt_conn_info info{};
    if (bt_conn_get_info(conn, &info) || info.state == BT_CONN_STATE_DISCONNECTED) return;
    auto &counts = *static_cast<ConnectionCounts *>(data);
    ++counts.total;
    if (pico::tracking::companion_identity(info.id)) ++counts.companion;
}
#endif
bool pico_authorized(bt_conn *conn, const bt_gatt_attr *attr)
{
    const auto address = reinterpret_cast<std::uintptr_t>(attr);
    const auto start = reinterpret_cast<std::uintptr_t>(pico_service.attrs);
    const auto end = start + pico_service.attr_count * sizeof(bt_gatt_attr);
    return address < start || address >= end || is_companion(conn);
}
const bt_gatt_authorization_cb authorization_callbacks = {
    .read_authorize = pico_authorized, .write_authorize = pico_authorized,
};
// A referenced connection owns each local window. No passkey is logged or stored.
K_MUTEX_DEFINE(pairing_mutex);
struct PairingWindow {
    bt_conn *connection{};
    std::uint32_t token{};
    std::int64_t deadline{};
    bool active{}, successful{}, fresh_pairing{};
} pairing_window;

bool pairing_live()
{ return pairing_window.active && k_uptime_get()/1000 < pairing_window.deadline; }

bool claim_pairing(bt_conn *conn, bool fresh = false)
{
    if (!is_companion(conn)) return false;
    k_mutex_lock(&pairing_mutex,K_FOREVER);
    const bool allowed=pairing_live() && (!pairing_window.connection || pairing_window.connection==conn);
    if (allowed && !pairing_window.connection) pairing_window.connection=bt_conn_ref(conn);
    if (allowed && fresh) pairing_window.fresh_pairing=true;
    k_mutex_unlock(&pairing_mutex);
    return allowed;
}

void pairing_result(bt_conn *conn,pico::EventKind kind,unsigned value,bool resumed = false)
{
    k_mutex_lock(&pairing_mutex,K_FOREVER);
    const bool owned=pairing_window.connection==conn && pairing_live() &&
        (!resumed || !pairing_window.fresh_pairing);
    const auto token=pairing_window.token;
    if (owned) {
        pairing_window.active=false;
        pairing_window.successful=kind==pico::EventKind::PairingComplete;
    }
    k_mutex_unlock(&pairing_mutex);
    if (owned && (event_sink==nullptr || !event_sink({kind,value,token}))) {
        // A full UI queue must never silently authorize an unseen pairing.
        bt_conn_auth_cancel(conn);
        bt_conn_disconnect(conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
}

bt_security_err accept_pairing(bt_conn *conn, const bt_conn_pairing_feat *)
{
    return claim_pairing(conn,true) ? BT_SECURITY_ERR_SUCCESS : BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}
void display_passkey(bt_conn *conn,unsigned passkey)
{
    if (!is_companion(conn)) return;
    k_mutex_lock(&pairing_mutex,K_FOREVER);
    const bool owned=pairing_window.connection==conn && pairing_live() && passkey<=999999U;
    const auto token=pairing_window.token;
    k_mutex_unlock(&pairing_mutex);
    if (!owned || event_sink==nullptr || !event_sink({pico::EventKind::PairingPasskey,passkey,token})) {
        pairing_result(conn,pico::EventKind::PairingFailed,BT_SECURITY_ERR_UNSPECIFIED);
        bt_conn_auth_cancel(conn);
        bt_conn_disconnect(conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
}
void cancel_pairing_request(bt_conn *conn)
{ pairing_result(conn,pico::EventKind::PairingFailed,BT_SECURITY_ERR_AUTH_FAIL); }
const bt_conn_auth_cb authentication_callbacks = {
    .pairing_accept = accept_pairing,
    .passkey_display = display_passkey,
    .cancel = cancel_pairing_request,
};

void complete_existing_bond(bt_conn *conn)
{
    if (bt_le_bond_exists(BT_ID_DEFAULT,bt_conn_get_dst(conn))) {
        // Fresh SMP still needs the pairing_complete callback after key distribution.
        pairing_result(conn,pico::EventKind::PairingComplete,0,true);
    }
}

void request_pin_security(bt_conn *conn)
{
    if (!claim_pairing(conn)) return;
    const int error=bt_conn_set_security(conn,BT_SECURITY_L4);
    if (error && error!=-EALREADY) {
        LOG_WRN("PIN security request failed (%d)",error);
        pairing_result(conn,pico::EventKind::PairingFailed,BT_SECURITY_ERR_UNSPECIFIED);
    }
    else if (bt_conn_get_security(conn)>=BT_SECURITY_L4)
        complete_existing_bond(conn);
}

int begin_advertising()
{
#if defined(CONFIG_PICO_FIND_HUB)
    ConnectionCounts counts;
    bt_conn_foreach(BT_CONN_TYPE_LE, count_connections, &counts);
    if (!pico::tracking::companion_advertising(counts.companion, counts.total,
            CONFIG_BT_MAX_CONN, CONFIG_BT_FAST_PAIR_FHN_MAX_CONN)) return 0;
#endif
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

#if defined(CONFIG_PICO_FIND_HUB)
    if (!is_companion(connection)) return;
#endif
    LOG_INF("BLE central connected");
    emit(pico::EventKind::Connected);
    request_pin_security(connection);
    LOG_INF("Companion connected; new pairing requires local Pair Phone window");
}

void disconnected(bt_conn *connection, std::uint8_t reason)
{
#if defined(CONFIG_PICO_FIND_HUB)
    if (!is_companion(connection)) return;
#else
    ARG_UNUSED(connection);
#endif
    pairing_result(connection,pico::EventKind::PairingFailed,BT_SECURITY_ERR_UNSPECIFIED);
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

void security_changed(bt_conn *conn, bt_security_t level, enum bt_security_err error)
{
    if (!is_companion(conn)) return;
    if (error != BT_SECURITY_ERR_SUCCESS) {
        LOG_WRN("BLE security failed (%u)", static_cast<unsigned int>(error));
        pairing_result(conn,pico::EventKind::PairingFailed,error);
        return;
    }

    LOG_INF("BLE link security level %u", static_cast<unsigned int>(level));
    if (level>=BT_SECURITY_L4) complete_existing_bond(conn);
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

void pairing_complete(bt_conn *conn, bool bonded)
{
    if (!is_companion(conn)) return;
    pairing_result(conn,bonded && bt_conn_get_security(conn)>=BT_SECURITY_L4 ?
        pico::EventKind::PairingComplete : pico::EventKind::PairingFailed,
        bonded && bt_conn_get_security(conn)>=BT_SECURITY_L4 ? 0 : BT_SECURITY_ERR_AUTH_REQUIREMENT);
    LOG_INF("BLE pairing complete; bonded=%u", static_cast<unsigned int>(bonded));
}

void pairing_failed(bt_conn *conn, enum bt_security_err reason)
{
    if (!is_companion(conn)) return;
    pairing_result(conn,pico::EventKind::PairingFailed,reason);
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
    if (bt_conn_auth_cb_register(&authentication_callbacks) ||
        bt_gatt_authorization_cb_register(&authorization_callbacks)) {
        LOG_ERR("Could not install identity isolation");
        return false;
    }
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

bool Ble::open_pairing(std::uint32_t token)
{
    if (!ready_) return false;
    close_pairing();
    k_mutex_lock(&pairing_mutex,K_FOREVER);
    pairing_window.token=token;
    pairing_window.deadline=k_uptime_get()/1000+120;
    pairing_window.active=true; pairing_window.successful=false; pairing_window.fresh_pairing=false;
    k_mutex_unlock(&pairing_mutex);
    LOG_INF("Local PIN pairing window opened (120 s)");
    bt_conn_foreach(BT_CONN_TYPE_LE,[](bt_conn *conn,void *) {
        bt_conn_info info{};
        if (bt_conn_get_info(conn,&info)==0 && info.state==BT_CONN_STATE_CONNECTED) request_pin_security(conn);
    },nullptr);
    return true;
}
void Ble::close_pairing()
{
    k_mutex_lock(&pairing_mutex,K_FOREVER);
    auto *conn=pairing_window.connection;
    const bool successful=pairing_window.successful;
    pairing_window.connection=nullptr; pairing_window.active=false; pairing_window.successful=false;
    k_mutex_unlock(&pairing_mutex);
    if (conn) {
        if (!successful) {
            bt_conn_auth_cancel(conn);
            bt_conn_disconnect(conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        }
        bt_conn_unref(conn);
    }
}

bool Ble::forget_companions()
{
    if (!ready_) return false;
    close_pairing();
    unsigned bond_count=0;
    bt_foreach_bond(BT_ID_DEFAULT,[](const bt_bond_info *,void *context) {
        ++*static_cast<unsigned *>(context);
    },&bond_count);
    LOG_INF("Local recovery requested; identity 0 has %u saved bond(s)",bond_count);
    // End failed/unbonded identity-0 sessions too. Never disconnect identity 1.
    bt_conn_foreach(BT_CONN_TYPE_LE,[](bt_conn *conn,void *) {
        bt_conn_info info{};
        if (bt_conn_get_info(conn,&info)==0 && info.id==BT_ID_DEFAULT) {
            const int error=bt_conn_disconnect(conn,BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            if (error && error!=-ENOTCONN) LOG_WRN("Companion disconnect failed (%d)",error);
        }
    },nullptr);
    // This API deletes only companion keys/CCCs using the SDK's persistence path.
    // Keep Bluetooth identity/address, pico/state, pico_clock/config and FP data.
    const int error=bt_unpair(BT_ID_DEFAULT,BT_ADDR_LE_ANY);
    if (error) { LOG_ERR("Companion bond reset failed (%d)",error); return false; }
    LOG_INF("Companion bonds cleared (identity 0); forget Pico on phone, then Bond again");
    // Existing connection-recycled callback restarts advertising after disconnect.
    // If no link was active, advertising was never stopped.
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

void Ble::publish_clock(const State &state,bool notify)
{
    if (!ready_) return;
    std::uint8_t encoded[protocol::kClockStatusSize]{};
    protocol::encode_clock(state,encoded);
    const auto settings=notify ? clock::encode(state.clock.settings) : clock::Blob{};
    k_mutex_lock(&status_mutex,K_FOREVER);
    std::copy(std::begin(encoded),std::end(encoded),clock_value.begin());
    if (notify) clock_config_value=settings;
    clock_uptime=state.uptime_seconds; stopwatch_uptime_ms=state.clock.monotonic_ms;
    k_mutex_unlock(&status_mutex);
    if (!notify) return;
#if defined(CONFIG_PICO_FIND_HUB)
    bt_conn_foreach(BT_CONN_TYPE_LE,[](bt_conn *conn,void *context) {
        if (is_companion(conn)) bt_gatt_notify(conn,&pico_service.attrs[clock_attr_index],context,protocol::kClockStatusSize);
    },encoded);
#else
    bt_gatt_notify(nullptr,&pico_service.attrs[clock_attr_index],encoded,sizeof(encoded));
#endif
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

#if defined(CONFIG_PICO_FIND_HUB)
    // Never broadcast private Pico status to a Fast Pair connection.
    bt_conn_foreach(BT_CONN_TYPE_LE, [](bt_conn *conn, void *) {
        if (!is_companion(conn)) return;
        std::array<std::uint8_t, protocol::kStatusSize> status{};
        std::array<std::uint8_t, protocol::kLocatorStatusSize> locator{};
        k_mutex_lock(&status_mutex, K_FOREVER);
        status = status_value;
        locator = locator_value;
        k_mutex_unlock(&status_mutex);
        bt_gatt_notify(conn, &pico_service.attrs[7], locator.data(), locator.size());
        bt_gatt_notify(conn, &pico_service.attrs[2], status.data(), status.size());
    }, nullptr);
#else
    bt_gatt_notify(nullptr,&pico_service.attrs[7],locator,sizeof(locator));
    const int error = bt_gatt_notify(nullptr, &pico_service.attrs[2], encoded,
                                     sizeof(encoded));
    if (error != 0 && error != -ENOTCONN) {
        LOG_DBG("Status notification not sent (%d)", error);
    }
#endif
}

} // namespace pico::platform
