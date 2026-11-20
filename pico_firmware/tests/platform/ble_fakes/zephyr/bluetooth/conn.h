#pragma once
#include <cstdint>
struct bt_conn { std::uint8_t id; bool connected; };
struct bt_conn_info { std::uint8_t id; int state; };
struct bt_conn_pairing_feat {};
using bt_security_t=unsigned;
enum bt_security_err { BT_SECURITY_ERR_SUCCESS, BT_SECURITY_ERR_AUTH_FAIL,
 BT_SECURITY_ERR_PIN_OR_KEY_MISSING, BT_SECURITY_ERR_OOB_NOT_AVAILABLE,
 BT_SECURITY_ERR_AUTH_REQUIREMENT, BT_SECURITY_ERR_PAIR_NOT_SUPPORTED,
 BT_SECURITY_ERR_PAIR_NOT_ALLOWED, BT_SECURITY_ERR_INVALID_PARAM,
 BT_SECURITY_ERR_KEY_REJECTED, BT_SECURITY_ERR_UNSPECIFIED };
struct bt_conn_auth_info_cb { void (*pairing_complete)(bt_conn *,bool); void (*pairing_failed)(bt_conn *,bt_security_err); };
struct bt_conn_auth_cb { bt_security_err (*pairing_accept)(bt_conn *,const bt_conn_pairing_feat *); };
struct bt_conn_cb {
 void (*connected)(bt_conn *,std::uint8_t);
 void (*disconnected)(bt_conn *,std::uint8_t);
 void (*recycled)();
 void (*security_changed)(bt_conn *,bt_security_t,bt_security_err);
};
#define BT_CONN_CB_DEFINE(name) bt_conn_cb name
#define BT_CONN_TYPE_LE 1
#define BT_CONN_STATE_DISCONNECTED 0
#define BT_SECURITY_L2 2
int bt_conn_get_info(bt_conn *,bt_conn_info *);
void bt_conn_foreach(int,void (*)(bt_conn *,void *),void *);
int bt_conn_disconnect(bt_conn *,std::uint8_t);
int bt_conn_auth_info_cb_register(bt_conn_auth_info_cb *);
int bt_conn_auth_info_cb_unregister(bt_conn_auth_info_cb *);
int bt_conn_auth_cb_register(const bt_conn_auth_cb *);
