#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int moss_lhp_start(void);
int moss_lhp_service(void); /* 0 pending, 1 complete, -1 failed */
void moss_lhp_stop(void);
size_t moss_lhp_peak_bytes(void);
size_t moss_lhp_live_bytes(void);
size_t moss_lhp_failures(void);
int moss_panel_begin(void);
int moss_panel_line(unsigned y, const uint8_t *rgb, size_t bytes);
int moss_panel_finish(void);
#ifdef __cplusplus
}
#endif
