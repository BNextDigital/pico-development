#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Checks geometry only. Does not enable Fast Pair, create identities, or write flash. */
int pico_find_hub_measurement_init(void);
#ifdef __cplusplus
}
#endif
