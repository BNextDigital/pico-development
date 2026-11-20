#pragma once
template<class... T> void fake_log(const char *,T...) {}
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) fake_log(__VA_ARGS__)
#define LOG_WRN(...) fake_log(__VA_ARGS__)
#define LOG_ERR(...) fake_log(__VA_ARGS__)
#define LOG_DBG(...) fake_log(__VA_ARGS__)
