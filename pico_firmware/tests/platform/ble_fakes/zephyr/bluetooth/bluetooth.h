#pragma once
#include "conn.h"
#include <cstddef>
struct bt_addr_le_t {};
struct bt_bond_info {};
void bt_foreach_bond(std::uint8_t,void (*)(const bt_bond_info *,void *),void *);
inline const bt_addr_le_t fake_any{};
#define BT_ADDR_LE_ANY (&fake_any)
#define BT_ID_DEFAULT 0
struct bt_data { int unused; };
#define BT_DATA_BYTES(...) {0}
#define BT_DATA(...) {0}
#define BT_DATA_FLAGS 1
#define BT_LE_AD_GENERAL 1
#define BT_LE_AD_NO_BREDR 2
#define BT_DATA_UUID128_ALL 1
#define BT_DATA_NAME_COMPLETE 1
#define BT_LE_ADV_CONN_FAST_1 nullptr
#define BT_UUID_128_ENCODE(...) 0
#define BT_UUID_DECLARE_128(...) nullptr
int bt_le_adv_start(const void *,const bt_data *,std::size_t,const bt_data *,std::size_t);
int bt_enable(void *);
int bt_unpair(std::uint8_t,const bt_addr_le_t *);
