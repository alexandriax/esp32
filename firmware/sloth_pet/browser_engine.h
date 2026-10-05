#ifndef MOSS_BROWSER_ENGINE_H
#define MOSS_BROWSER_ENGINE_H
#include <stddef.h>
#include <stdint.h>
#include "browser_viewport.h"
#ifdef __cplusplus
extern "C" {
#endif

enum BrowserEngineState {
    BROWSER_ENGINE_FAILED = -1, BROWSER_ENGINE_IDLE = 0,
    BROWSER_ENGINE_LAYOUT, BROWSER_ENGINE_PAINTING, BROWSER_ENGINE_READY
};
enum BrowserEngineError {
    BROWSER_ENGINE_ERROR_NONE = 0, BROWSER_ENGINE_ERROR_INPUT,
    BROWSER_ENGINE_ERROR_MEMORY, BROWSER_ENGINE_ERROR_LAYOUT,
    BROWSER_ENGINE_ERROR_PANEL, BROWSER_ENGINE_ERROR_TIMEOUT
};
/* The caller keeps html alive, unchanged, until stop. Only one engine is active.
 * Budget counts requested LWS allocations, excluding malloc metadata and stacks.
 * Network access stays disabled. Optional assets must be prepared by the caller.
 */
typedef struct BrowserEngineAssets {
    void *user;
    /* index >=0 on success, -1 if absent; bounded intrinsic pixel dimensions. */
    int (*lookup)(void *user, const char *src, unsigned *width, unsigned *height);
    /* Returned RGBA row stays valid until next row call; NULL skips the image. */
    const uint8_t *(*row)(void *user, unsigned index, unsigned y);
} BrowserEngineAssets;
int browser_engine_start_assets(const char *html, size_t bytes, size_t heap_budget,
                                const BrowserEngineAssets *assets);
int browser_engine_start(const char *html, size_t bytes, size_t heap_budget);
/* One call parses at most 512 bytes or renders at most four 480-pixel rows.
 * An individual upstream parsing / layout operation is not preemptible. */
enum BrowserEngineState browser_engine_service(void);
void browser_engine_stop(void);
/* Reset the layout/paint watchdog after dismissing a user-controlled modal. */
void browser_engine_resume(void);
/* Absolute document offset, clamped. Allowed after layout, including mid-paint.
 * The current offset explicitly repaints (e.g. after dismissing the URL editor). */
int browser_engine_scroll(int absolute_y);
/* Native viewport coordinates (480 x BROWSER_VIEW_HEIGHT). 1 copied, 0 none, -1 too long/invalid.
 * Returns the original href; the caller resolves it against the fetched URL. */
int browser_engine_link_at(unsigned x, unsigned y, char *out, size_t capacity);
unsigned browser_engine_scroll_y(void);
unsigned browser_engine_content_height(void);
int browser_engine_clipped(void);
enum BrowserEngineError browser_engine_error(void);
size_t browser_engine_peak_bytes(void);
size_t browser_engine_live_bytes(void);
size_t browser_engine_failures(void);
/* Firmware callbacks: relative rows 0..BROWSER_VIEW_HEIGHT-1, tightly packed RGB888 (1440 bytes).
 * Return nonzero on success. begin must discard any interrupted previous paint. */
int browser_panel_begin(void);
int browser_panel_line(unsigned relative_y, const uint8_t *rgb, size_t bytes);
int browser_panel_finish(void);
#ifdef __cplusplus
}
#endif
#endif
