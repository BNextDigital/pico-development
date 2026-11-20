// Production BLE adapter with an in-memory API model, not a live radio test.
#define CONFIG_PICO_LOG_LEVEL 3
#define CONFIG_PICO_FIND_HUB 1
#define CONFIG_BT_MAX_CONN 4
#define CONFIG_BT_FAST_PAIR_FHN_MAX_CONN 2
#define CONFIG_BT_DEVICE_NAME "Pico"
#include "platform/pico_ble.cpp"
#include <cassert>
#include <vector>
#include <iostream>
namespace {
bt_conn companion{0,true}, network{1,true};
std::vector<pico::Event> events;
unsigned disconnected_companion, disconnected_network;
int unpair_result;
unsigned unpair_identity=255;
const bt_addr_le_t *unpair_address;
bool accepting=true;
bool sink(const pico::Event &event) { if (!accepting) return false; events.push_back(event); return true; }
}
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
int main() {
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
 std::cout << "BLE adapter identity-scoped reset and atomic command forwarding models passed\n";
}
