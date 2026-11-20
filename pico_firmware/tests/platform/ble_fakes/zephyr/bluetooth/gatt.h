#pragma once
#include "conn.h"
#include <cstddef>
#include <sys/types.h>
struct bt_gatt_attr;
using fake_read=ssize_t (*)(bt_conn *,const bt_gatt_attr *,void *,std::uint16_t,std::uint16_t);
using fake_write=ssize_t (*)(bt_conn *,const bt_gatt_attr *,const void *,std::uint16_t,std::uint16_t,std::uint8_t);
struct bt_gatt_attr { fake_read read; fake_write write; void (*subscription)(const bt_gatt_attr *,std::uint16_t); };
struct fake_service { bt_gatt_attr *attrs; std::size_t attr_count; };
struct bt_gatt_authorization_cb { bool (*read_authorize)(bt_conn *,const bt_gatt_attr *); bool (*write_authorize)(bt_conn *,const bt_gatt_attr *); };
#define BT_GATT_SERVICE_DEFINE(name,...) bt_gatt_attr name##_attrs[]={__VA_ARGS__}; fake_service name{name##_attrs,sizeof(name##_attrs)/sizeof(bt_gatt_attr)}
#define BT_GATT_PRIMARY_SERVICE(...) {nullptr,nullptr,nullptr}
#define BT_GATT_CHARACTERISTIC(uuid,props,perm,read,write,user) {read,write,nullptr},{read,write,nullptr}
#define BT_GATT_CCC(changed,perm) {nullptr,nullptr,changed}
#define BT_GATT_CCC_NOTIFY 1
#define BT_GATT_CHRC_READ 1
#define BT_GATT_CHRC_NOTIFY 2
#define BT_GATT_CHRC_WRITE 4
#define BT_GATT_CHRC_WRITE_WITHOUT_RESP 8
#define BT_GATT_PERM_READ_ENCRYPT 1
#define BT_GATT_PERM_WRITE_ENCRYPT 2
ssize_t bt_gatt_attr_read(bt_conn *,const bt_gatt_attr *,void *,std::uint16_t,std::uint16_t,const void *,std::size_t);
int bt_gatt_notify(bt_conn *,const bt_gatt_attr *,const void *,std::size_t);
int bt_gatt_authorization_cb_register(const bt_gatt_authorization_cb *);
