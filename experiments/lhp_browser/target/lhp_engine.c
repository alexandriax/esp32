/* The upstream embedded LHP and api-test-lhp-dlo examples are CC0-1.0.
 * This isolated probe uses their public parser / scanline interfaces.
 */
#include "lhp_engine.h"
#include <libwebsockets.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define LHP_HEAP_LIMIT (192u * 1024u)
#define LINE_BYTES (480u * 3u)
static const uint8_t regular16[] = {
#include "FiraSansCondensed-Regular16.mcufont.h"
};
static const uint8_t regular20[] = {
#include "FiraSansCondensed-Regular20.mcufont.h"
};
static const uint8_t regular24[] = {
#include "FiraSansCondensed-Regular24.mcufont.h"
};
static const uint8_t regular32[] = {
#include "FiraSansCondensed-Regular32.mcufont.h"
};
static const uint8_t bold20[] = {
#include "FiraSansCondensed-Bold20.mcufont.h"
};
static const uint8_t bold32[] = {
#include "FiraSansCondensed-Bold32.mcufont.h"
};
static const char fixture[] =
    "<!doctype html><html><head><meta charset='utf-8'><title>Moss LHP</title>"
    "<style>body{font-family:sans;font-size:20px;padding:22px;background:#f5f0df;color:#173d32}"
    "h1{font-size:32px;color:#215c41}p{margin-bottom:18px}a{color:#245dcc}"
    "</style></head><body><h1>Moss browser lab</h1>"
    "<p>This page is HTML and CSS, laid out directly on the ESP32-C6.</p>"
    "<p>Native 480-pixel text. Two-row display stripes. No full framebuffer.</p>"
    "<p><a href='file://dlofs/fixture.html'>A real parsed link</a></p>"
    "<p>Offline fixture. Wi-Fi stays off. Press KEY to render again.</p>"
    "</body></html>";

/* malloc alignment must survive accounting metadata. The limit covers all
 * requested LWS allocations, but excludes allocator/RTOS/driver overhead. */
typedef union { max_align_t alignment; size_t bytes; } allocation_header;
static size_t live_bytes, peak_bytes, failures;
static void *bounded_realloc(void *ptr, size_t bytes, const char *reason)
{
    allocation_header *old = ptr ? (allocation_header *)ptr - 1 : NULL;
    const size_t previous = old ? old->bytes : 0;
    allocation_header *next;
    (void)reason;
    if (!bytes) { live_bytes -= previous; free(old); return NULL; }
    if (bytes > SIZE_MAX - sizeof(*next) || bytes > LHP_HEAP_LIMIT ||
        live_bytes - previous > LHP_HEAP_LIMIT - bytes) { failures++; return NULL; }
    next = realloc(old, sizeof(*next) + bytes);
    if (!next) { failures++; return NULL; }
    next->bytes = bytes;
    live_bytes = live_bytes - previous + bytes;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return next + 1;
}
static struct lws_context *context;
static lws_display_render_state_t state;
static struct lws_plat_file_ops fixture_ops;
static lws_dlo_filesystem_t fixture_file;
static lws_sorted_usec_list_t deadline, input_tick;
static lws_surface_info_t surface = {
    .wh_px = {{480, 0}, {480, 0}}, .wh_mm = {{39, 0}, {39, 0}},
    .type = LWSSURF_TRUECOLOR32, .greyscale = 0
};
static int result;
/* Bound lws_service sleep so the sketch polls KEY/BOOT between batches. */
static void tick_callback(lws_sorted_usec_list_t *sul)
{
    lws_sul_schedule(context, 0, sul, tick_callback, 10 * LWS_US_PER_MS);
}
static void timeout_callback(lws_sorted_usec_list_t *sul)
{
    (void)sul; result = -1;
}
static void render(lws_sorted_usec_list_t *sul)
{
    lws_display_render_state_t *rs = lws_container_of(sul, lws_display_render_state_t, sul);
    if (rs->html == 1 || result) return;
    if (!rs->line) {
        if (lws_display_get_ids_boxes(rs) & LWS_SRET_FATAL) { result = -1; return; }
        rs->line = bounded_realloc(NULL, LINE_BYTES, "Moss RGB row");
        if (!rs->line || !moss_panel_begin()) { result = -1; return; }
        memset(rs->line, 0xff, LINE_BYTES);
        rs->curr = 0;
    }
    for (unsigned budget = 0; budget < 4 && rs->curr < 480; ++budget) {
        const lws_stateful_ret_t status = lws_display_list_render_line(rs);
        if (status & LWS_SRET_FATAL) { result = -1; return; }
        if (status) {
            lws_sul_schedule(context, 0, &rs->sul, render, LWS_US_PER_MS);
            return;
        }
        if (!moss_panel_line((unsigned)rs->curr, (const uint8_t *)rs->line, LINE_BYTES)) {
            result = -1; return;
        }
        ++rs->curr;
        memset(rs->line, 0xff, LINE_BYTES);
    }
    if (rs->curr == 480) result = moss_panel_finish() && !failures ? 1 : -1;
    else lws_sul_schedule(context, 0, &rs->sul, render, 1);
}
int moss_lhp_start(void)
{
    struct lws_context_creation_info info;
    lws_lhp_filter_t filter = {.block_rules = "http://\nhttps://\n"};
    if (context || live_bytes) return -1;
    memset(&state, 0, sizeof(state));
    memset(&fixture_file, 0, sizeof(fixture_file));
    peak_bytes = failures = 0; result = 0;
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
    lws_set_allocator(bounded_realloc);
    lws_context_info_defaults(&info, NULL);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.fd_limit_per_thread = 8;
    info.max_http_header_data = 4096;
    info.pt_serv_buf_size = 4096;
    context = lws_create_context(&info);
    if (!context) return -1;
    fixture_ops = lws_dlo_fops;
    fixture_ops.cx = context;
    fixture_ops.next = lws_get_fops(context)->next;
    lws_get_fops(context)->next = &fixture_ops;
    fixture_file.name = "fixture.html";
    fixture_file.data = fixture;
    fixture_file.len = sizeof(fixture) - 1;
    if (!lws_dlo_file_register(context, &fixture_file) ||
        lws_font_register(context, regular16, sizeof(regular16)) ||
        lws_font_register(context, regular20, sizeof(regular20)) ||
        lws_font_register(context, regular24, sizeof(regular24)) ||
        lws_font_register(context, regular32, sizeof(regular32)) ||
        lws_font_register(context, bold20, sizeof(bold20)) ||
        lws_font_register(context, bold32, sizeof(bold32))) {
        result = -1; moss_lhp_stop(); return -1;
    }
    state.ic = &surface;
    lws_sul_schedule(context, 0, &input_tick, tick_callback, 10 * LWS_US_PER_MS);
    lws_sul_schedule(context, 0, &deadline, timeout_callback, 15 * LWS_US_PER_SEC);
    if (lws_lhp_ss_browse_filter(context, &state, "file://dlofs/fixture.html", render, &filter)) {
        result = -1; moss_lhp_stop(); return -1;
    }
    return 0;
}
int moss_lhp_service(void)
{
    if (!context) return -1;
    if (!result && lws_service(context, 0) < 0) result = -1;
    return result;
}
void moss_lhp_stop(void)
{
    if (!context) return;
    lws_sul_cancel(&deadline);
    lws_sul_cancel(&input_tick);
    lws_lhp_ss_cancel(&state);
    lws_sul_cancel(&state.sul);
    bounded_realloc(state.line, 0, "Moss RGB row"); state.line = NULL;
    lws_display_list_destroy(context, &state.displaylist);
    lws_context_destroy(context); context = NULL;
}
size_t moss_lhp_peak_bytes(void) { return peak_bytes; }
size_t moss_lhp_live_bytes(void) { return live_bytes; }
size_t moss_lhp_failures(void) { return failures; }
