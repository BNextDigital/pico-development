// Production BLE adapter with an in-memory API model, not a live radio test.
#define CONFIG_PICO_LOG_LEVEL 3
#define CONFIG_PICO_FIND_HUB 1
#define CONFIG_BT_MAX_CONN 4
#define CONFIG_BT_FAST_PAIR_FHN_MAX_CONN 2
#define CONFIG_BT_DEVICE_NAME "Pico"
#include "platform/pico_ble.cpp"
#include <cassert>
#include <vector>
#include <cstdio>
#include <iostream>
#include <string_view>
namespace {
bt_conn companion{0,true}, network{1,true};
std::vector<pico::Event> events;
unsigned disconnected_companion, disconnected_network;
int unpair_result;
unsigned unpair_identity=255;
const bt_addr_le_t *unpair_address;
bool accepting=true;
unsigned auth_cancels=0,security_requests=0;
int security_error=0;
bool companion_bonded=false;
bt_addr_le_t companion_address{};
bool sink(const pico::Event &event) { if (!accepting) return false; events.push_back(event); return true; }
}
const bt_addr_le_t *bt_conn_get_dst(const bt_conn *conn) {
 assert(conn==&companion); return &companion_address;
}
bool bt_le_bond_exists(std::uint8_t id,const bt_addr_le_t *address) {
 return id==0 && address==&companion_address && companion_bonded;
}
bt_conn *bt_conn_ref(bt_conn *conn) { ++conn->refs; return conn; }
void bt_conn_unref(bt_conn *conn) { assert(conn->refs); --conn->refs; }
int bt_conn_auth_cancel(bt_conn *) { ++auth_cancels; return 0; }
int bt_conn_set_security(bt_conn *conn,bt_security_t level) {
 assert(conn->id==0 && level==BT_SECURITY_L4); ++security_requests; return security_error;
}
bt_security_t bt_conn_get_security(bt_conn *conn) { return conn->security; }
int bt_conn_get_info(bt_conn *conn,bt_conn_info *info) { *info={conn->id,conn->connected ? 1 : 0}; return 0; }
void bt_conn_foreach(int,void (*cb)(bt_conn *,void *),void *data) { cb(&companion,data); cb(&network,data); }
int bt_conn_disconnect(bt_conn *conn,std::uint8_t reason) {
 assert(reason==BT_HCI_ERR_REMOTE_USER_TERM_CONN);
 if(conn->id==0) ++disconnected_companion; else ++disconnected_network;
 conn->connected=false; return 0;
}
int bt_conn_auth_info_cb_register(bt_conn_auth_info_cb *) { return 0; }
int bt_conn_auth_info_cb_unregister(bt_conn_auth_info_cb *) { return 0; }
int bt_conn_auth_cb_register(const bt_conn_auth_cb *) { return 0; }
int bt_gatt_authorization_cb_register(const bt_gatt_authorization_cb *) { return 0; }
int bt_enable(void *) { return 0; }
void bt_foreach_bond(std::uint8_t id,void (*cb)(const bt_bond_info *,void *),void *context) {
 assert(id==0); bt_bond_info info{}; cb(&info,context);
}
int bt_le_adv_start(const void *,const bt_data *,std::size_t,const bt_data *,std::size_t) { return 0; }
int bt_unpair(std::uint8_t id,const bt_addr_le_t *addr) { unpair_identity=id;unpair_address=addr;return unpair_result; }
ssize_t bt_gatt_attr_read(bt_conn *,const bt_gatt_attr *,void *out,std::uint16_t n,std::uint16_t off,const void *data,std::size_t size) {
 if(off>size) return -7;
 const auto copied=std::min<std::size_t>(n,size-off); std::memcpy(out,static_cast<const char *>(data)+off,copied); return copied;
}
int bt_gatt_notify(bt_conn *,const bt_gatt_attr *,const void *,std::size_t) { return 0; }
int main(int argc, char **argv) {
 assert(argc<=2);
 const bool export_clock=argc==2 && std::string_view(argv[1])=="--clock-fixture";
 assert(argc==1 || export_clock);
 pico::platform::Ble ble;
 assert(!ble.forget_companions()); assert(ble.init(sink));
 connection_callbacks.connected(&network,0); assert(events.empty());
 connection_callbacks.connected(&companion,0); assert(events.size()==1 && events[0].kind==pico::EventKind::Connected);
 events.clear();
 assert(ble.forget_companions());
 assert(unpair_identity==0 && unpair_address==BT_ADDR_LE_ANY);
 assert(disconnected_companion==1 && disconnected_network==0 && network.connected);
 unpair_result=-EIO; assert(!ble.forget_companions());
 // Actual adapter command callback must forward the complete atomic Event.
 const std::uint8_t packet[]{0x60,0x00,0x7f,0xff,0x6a,0xd4,0xfe};
 assert(write_command(&companion,nullptr,packet,sizeof(packet),0,0)==sizeof(packet));
 assert(events.back().kind==pico::EventKind::SyncTime && events.back().auxiliary==540);
 const std::uint8_t alarm[]{0x62,2,0xa4,1,31,1};
 assert(write_command(&companion,nullptr,alarm,sizeof(alarm),0,0)==sizeof(alarm));
 assert(events.back().value==2 && events.back().auxiliary==(420U|(31U<<16)|(1U<<24)));
 const auto count=events.size(); accepting=false;
 assert(write_command(&companion,nullptr,packet,sizeof(packet),0,0)==-BT_ATT_ERR_INSUFFICIENT_RESOURCES);
 assert(events.size()==count);
 assert(!pico_authorized(&network,&pico_service.attrs[10]));
 assert(!pico_authorized(&network,&pico_service.attrs[13]));
 assert(pico_authorized(&companion,&pico_service.attrs[10]));
 // Read the production attributes through a 23-byte ATT MTU model.
 pico::State clock_state;
 clock_state.clock.valid=true;
 clock_state.clock.sync_due=false;
 clock_state.clock.utc=1795381200;
 clock_state.clock.sync_age=10;
 clock_state.clock.settings.offset_minutes=-300;
 clock_state.clock.settings.hour24=false;
 clock_state.clock.settings.glance=true;
 clock_state.clock.settings.alarms[0]={420,31,true,-1};
 clock_state.clock.settings.alarms[2]={1230,0,true,20780};
 clock_state.clock.ringing=1;
 clock_state.clock.stopwatch_seconds=90;
 clock_state.clock.stopwatch_fraction_ms=250;
 ble.publish_clock(clock_state);
 std::array<std::uint8_t,20> wire_status;
 pico::clock::Blob wire_config;
 assert(read_clock(&companion,nullptr,wire_status.data(),22,0)==20);
 assert(read_clock_config(&companion,nullptr,wire_config.data(),22,0)==22);
 assert(read_clock_config(&companion,nullptr,wire_config.data()+22,22,22)==22);
 assert(read_clock_config(&companion,nullptr,wire_config.data(),22,44)==0);
 assert(read_clock_config(&companion,nullptr,wire_config.data(),22,45)<0);
 assert(wire_config==pico::clock::encode(clock_state.clock.settings));
 auto print_hex=[](const auto &bytes) {
     for (const auto byte : bytes) std::printf("%02x",unsigned(byte));
 };
 if (export_clock) {
     std::printf("{\n  \"status\": \"");
     print_hex(wire_status);
     std::printf("\",\n  \"config\": \"");
     print_hex(wire_config);
     std::printf("\"\n}\n");
     return 0;
 }
 // Local window and actual production authentication callbacks.
 accepting=true; events.clear(); companion.connected=false; network.connected=true;
 assert(accept_pairing(&companion,nullptr)==BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
 assert(ble.open_pairing(10));
 assert(accept_pairing(&network,nullptr)==BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
 assert(accept_pairing(&companion,nullptr)==BT_SECURITY_ERR_SUCCESS);
 bt_conn intruder{0,true};
 assert(accept_pairing(&intruder,nullptr)==BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
 authentication_callbacks.passkey_display(&companion,37);
 assert(events.back().kind==pico::EventKind::PairingPasskey && events.back().value==37 && events.back().auxiliary==10);
 companion.security=BT_SECURITY_L4; companion_bonded=true;
 const auto before_fresh_encryption=events.size();
 security_changed(&companion,BT_SECURITY_L4,BT_SECURITY_ERR_SUCCESS);
 assert(events.size()==before_fresh_encryption+1 && events.back().kind==pico::EventKind::RequestStatus);
 pairing_callbacks.pairing_complete(&companion,true);
 assert(events.back().kind==pico::EventKind::PairingComplete && events.back().auxiliary==10);
 const auto disconnects=disconnected_companion;
 ble.close_pairing(); assert(companion.refs==0 && disconnected_companion==disconnects);
 // Resuming a saved bond emits security_changed without pairing_complete.
 companion.connected=true; companion.security=2;
 assert(ble.open_pairing(16));
 companion.security=BT_SECURITY_L4;
 security_changed(&companion,BT_SECURITY_L4,BT_SECURITY_ERR_SUCCESS);
 assert(events[events.size()-2].kind==pico::EventKind::PairingComplete);
 assert(events[events.size()-2].auxiliary==16);
 ble.close_pairing(); assert(companion.refs==0 && disconnected_companion==disconnects);
 companion_bonded=false;
 // Cancel, strict deadline, late callbacks and identity separation.
 companion.security=2; assert(ble.open_pairing(11));
 assert(accept_pairing(&companion,nullptr)==BT_SECURITY_ERR_SUCCESS);
 fake_uptime=121000; // Exactly 120 seconds after opening.
 assert(accept_pairing(&intruder,nullptr)==BT_SECURITY_ERR_PAIR_NOT_ALLOWED);
 const auto before_expired=events.size();
 authentication_callbacks.passkey_display(&companion,123456);
 assert(events.size()==before_expired); // Expired code never reaches UI.
 ble.close_pairing(); assert(companion.refs==0 && network.connected);
 fake_uptime=122000; assert(ble.open_pairing(12));
 companion.connected=true;
 connected(&companion,0); assert(security_requests>0);
 accepting=false;
 const auto cancels=auth_cancels;
 authentication_callbacks.passkey_display(&companion,654321);
 assert(auth_cancels>cancels && !companion.connected);
 accepting=true; ble.close_pairing(); assert(companion.refs==0);
 assert(ble.open_pairing(13));
 assert(accept_pairing(&companion,nullptr)==BT_SECURITY_ERR_SUCCESS);
 pairing_callbacks.pairing_failed(&companion,BT_SECURITY_ERR_AUTH_FAIL);
 assert(events.back().kind==pico::EventKind::PairingFailed && events.back().auxiliary==13);
 ble.close_pairing(); assert(companion.refs==0);
 // No identity-1 UI callback may cancel its separate transport.
 const auto network_cancels=auth_cancels;
 authentication_callbacks.passkey_display(&network,123456);
 assert(network.connected && auth_cancels==network_cancels);
 // Low-memory/security API failure fails closed and releases its reference.
 companion.connected=true; security_error=-ENOMEM;
 assert(ble.open_pairing(15));
 assert(events.back().kind==pico::EventKind::PairingFailed && events.back().auxiliary==15);
 ble.close_pairing(); assert(companion.refs==0);
 security_error=0;
 // An unauthenticated/nonbonded completion is not success.
 assert(ble.open_pairing(14));
 assert(accept_pairing(&companion,nullptr)==BT_SECURITY_ERR_SUCCESS);
 pairing_callbacks.pairing_complete(&companion,false);
 assert(events.back().kind==pico::EventKind::PairingFailed);
 ble.close_pairing(); assert(companion.refs==0);
 std::cout << "BLE adapter identity-scoped reset and atomic command forwarding models passed\n";
}
