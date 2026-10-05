/* Uses the pinned libwebsockets LHP parser and retained display-list renderer.
 * Font assets are Fira Sans Condensed, distributed under the SIL OFL. */
#include "browser_engine.h"
#if defined(ESP_PLATFORM)
#include "browser_memory.h"
#define free browser_memory_free
#define realloc browser_memory_realloc
#endif
#include <libwebsockets.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#else
#include <time.h>
#endif

#include "browser_viewport.h"
#define VIEW_WIDTH BROWSER_VIEW_WIDTH
#define VIEW_HEIGHT BROWSER_VIEW_HEIGHT
#define DOCUMENT_HEIGHT 8192u
#define HTML_LIMIT (64u * 1024u)
#define HEAP_LIMIT (128u * 1024u)
#define LINE_BYTES (480u * 3u)
#define TIME_LIMIT_US (15u * 1000000u)
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
typedef union { max_align_t alignment; size_t bytes; } allocation_header;
static size_t live_bytes, peak_bytes, failures, budget_bytes;
static struct lws_context *context;
static lhp_ctx_t *parser;
static lws_display_render_state_t render_state;
static lws_dl_rend_t layout;
static enum BrowserEngineState phase;
static enum BrowserEngineError error;
static const uint8_t *document;
static size_t document_bytes, parsed_bytes;
static unsigned scroll_y, content_height;
static int parser_constructed, parse_complete, parse_failed, clipped;
static uint64_t deadline;
static BrowserEngineAssets assets;
static const lws_surface_info_t surface = {
    .wh_px = {{VIEW_WIDTH, 0}, {DOCUMENT_HEIGHT, 0}},
    .wh_mm = {{39, 0}, {666, 0}},
    .type = LWSSURF_TRUECOLOR32, .greyscale = 0
};

static uint64_t now_us(void)
{
#if defined(BROWSER_ENGINE_TEST_CLOCK)
    extern uint64_t browser_engine_test_now_us(void);
    return browser_engine_test_now_us();
#elif defined(ESP_PLATFORM)
    return (uint64_t)esp_timer_get_time();
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
#endif
}
static void *bounded_realloc(void *ptr, size_t bytes, const char *reason)
{
    allocation_header *old = ptr ? (allocation_header *)ptr - 1 : NULL;
    const size_t previous = old ? old->bytes : 0;
    allocation_header *next;
    (void)reason;
    if (!bytes) { live_bytes -= previous; free(old); return NULL; }
    if (bytes > SIZE_MAX - sizeof(*next) || bytes > budget_bytes ||
        live_bytes - previous > budget_bytes - bytes) {
        failures++; return NULL;
    }
    next = realloc(old, sizeof(*next) + bytes);
    if (!next) { failures++; return NULL; }
    next->bytes = bytes;
    live_bytes = live_bytes - previous + bytes;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return next + 1;
}
static void destroy_parser(void)
{
    if (!parser) return;
    if (parser_constructed) lws_lhp_destruct(parser);
    bounded_realloc(parser, 0, "browser parser");
    parser = NULL; parser_constructed = 0;
}
static void cleanup(void)
{
    destroy_parser();
    bounded_realloc(render_state.line, 0, "browser RGB row");
    render_state.line = NULL;
    if (context) {
        lws_display_list_destroy(context, &render_state.displaylist);
        lws_display_render_free_ids(&render_state);
        lws_context_destroy(context); context = NULL;
    }
    document = NULL; document_bytes = parsed_bytes = 0;
}
static enum BrowserEngineState fail(enum BrowserEngineError why)
{
    cleanup(); error = why; phase = BROWSER_ENGINE_FAILED;
    return phase;
}
typedef struct { lws_dlo_t dlo;unsigned index,width,height; } cached_image;
static lws_stateful_ret_t render_image(lws_display_render_state_t *rs)
{
    cached_image *im = (cached_image *)rs->st[rs->sp].dlo;
    lws_fx_t ax, ay;
    lws_fx_add(&ax, &rs->st[rs->sp].co.x, &im->dlo.box.x);
    lws_fx_add(&ay, &rs->st[rs->sp].co.y, &im->dlo.box.y);
    int bw = im->dlo.box.w.whole, bh = im->dlo.box.h.whole;
    int y = rs->curr - ay.whole;
    if (bw <= 0 || bh <= 0 || y < 0 || y >= bh) return LWS_SRET_OK;
    const uint8_t *pixels = assets.row(assets.user, im->index,
                                      (unsigned)((uint64_t)y * im->height / bh));
    if (!pixels) return LWS_SRET_OK;
    int first = ax.whole < 0 ? 0 : ax.whole;
    int64_t right = (int64_t)ax.whole + bw;
    int end = right > VIEW_WIDTH ? VIEW_WIDTH : (int)right;
    for (int x = first; x < end; ++x) {
        const uint8_t *p = pixels + ((uint64_t)(x - ax.whole) * im->width / bw) * 4;
        uint8_t *out = rs->line + x * 3;
        for (unsigned c = 0; c < 3; ++c)
            out[c] = (uint8_t)((p[c] * p[3] + out[c] * (255 - p[3]) + 127) / 255);
    }
    return LWS_SRET_OK;
}
static void add_image(lhp_ctx_t *ctx)
{
    lws_dll2_t *tail = lws_dll2_get_tail(&ctx->stack);
    if (!tail || !assets.lookup || !assets.row) return;
    lhp_pstack_t *ps = lws_container_of(tail, lhp_pstack_t, list);
    lws_dll2_t *head = lws_dll2_get_head(&ps->atr);
    const lhp_atr_t *tag = head ? lws_container_of(head, lhp_atr_t, list) : NULL;
    if (!tag || tag->name_len != 3 || strncasecmp((const char *)&tag[1], "img", 3) || ps->hidden || !ps->in_body || ps->dlo) return;
    const char *src = lws_html_get_atr(ps, "src", 3);
    if (!src) return;
    unsigned width = 0, height = 0;
    int index = assets.lookup(assets.user, src, &width, &height);
    if (index < 0 || !width || !height || width > 320 || height > 320) return;
    lhp_pstack_t *parent = NULL;
    for (lws_dll2_t *d = lws_dll2_get_prev(tail); d; d = lws_dll2_get_prev(d)) {
        lhp_pstack_t *candidate = lws_container_of(d, lhp_pstack_t, list);
        if (candidate->is_block && candidate->dlo) { parent = candidate; break; }
    }
    if (!parent) return;
    cached_image *im = bounded_realloc(NULL, sizeof(*im), "cached browser image");
    if (!im) return;
    memset(im, 0, sizeof(*im));
    im->index = (unsigned)index; im->width = width; im->height = height;
    im->dlo.render = render_image;
    lws_fx_set(im->dlo.box.w, (int)width, 0);
    lws_fx_set(im->dlo.box.h, (int)height, 0);
    lws_display_dlo_add(layout.dl, parent->dlo, &im->dlo);
    ps->dlo = &im->dlo;
}
static lws_stateful_ret_t layout_callback(lhp_ctx_t *ctx, char reason)
{
    if (reason == LHPCB_ELEMENT_START) add_image(ctx);
    lws_stateful_ret_t r = lhp_displaylist_layout(ctx, reason);
    if (reason == LHPCB_COMPLETE) parse_complete = 1;
    if (reason == LHPCB_FAILED) parse_failed = 1;
    return r;
}
static int supported_depth(void)
{
    lws_dll2_t *first = lws_dll2_get_head(&render_state.displaylist.dl);
    unsigned depth = 0;
    if (!first) return 1;
    /* Reuse the static render stack rather than another stack-sized local.
     * Upstream silently skips deeper children; fail before publishing a page. */
    memset(render_state.st, 0, sizeof(render_state.st));
    render_state.st[0].dlo = lws_container_of(first, lws_dlo_t, list);
    while (depth || render_state.st[0].dlo) {
        lws_dlo_t *dlo = render_state.st[depth].dlo;
        lws_dll2_t *next, *child;
        if (!dlo) { --depth; continue; }
        next = lws_dll2_get_next(&dlo->list);
        render_state.st[depth].dlo = next ? lws_container_of(next, lws_dlo_t, list) : NULL;
        child = lws_dll2_get_head(&dlo->children);
        if (child) {
            if (depth + 1 == LWS_DLO_STACK_DEPTH) return 0;
            render_state.st[++depth].dlo = lws_container_of(child, lws_dlo_t, list);
        }
    }
    return 1;
}
static void measure_document(void)
{
    lws_dll2_t *d = lws_dll2_get_head(&render_state.displaylist.dl);
    content_height = VIEW_HEIGHT;
    /* LHP's root background spans the offered surface. Its direct children
     * carry the actual content extents; this matches upstream's window viewer. */
    if (d) {
        lws_dlo_t *root = lws_container_of(d, lws_dlo_t, list);
        lws_start_foreach_dll(lws_dll2_t *, c, lws_dll2_get_head(&root->children)) {
            lws_dlo_t *child = lws_container_of(c, lws_dlo_t, list);
            int64_t bottom = (int64_t)child->box.y.whole + child->box.h.whole +
                             root->box.y.whole + (child->box.h.frac != 0);
            if (bottom > DOCUMENT_HEIGHT) { clipped = 1; bottom = DOCUMENT_HEIGHT; }
            if (bottom > content_height) content_height = (unsigned)bottom;
        } lws_end_foreach_dll(c);
    }
    if (layout.clipped) { clipped = 1; content_height = DOCUMENT_HEIGHT; }
}
static int begin_paint(unsigned offset)
{
    scroll_y = offset;
    memset(render_state.st, 0, sizeof(render_state.st));
    render_state.sp = 0;
    render_state.curr = (lws_display_scalar)offset;
    deadline = now_us() + TIME_LIMIT_US;
    if (!browser_panel_begin()) { fail(BROWSER_ENGINE_ERROR_PANEL); return -1; }
    phase = BROWSER_ENGINE_PAINTING;
    return 0;
}
/* Page-derived parser diagnostics must not reach libc stderr: besides exposing
 * page content, its first-use stream buffer can outlive the released canvas.
 * The application reports bounded engine errors through its UI and snapshot. */
static void private_log(int level, const char *line) { (void)level; (void)line; }
int browser_engine_start(const char *html, size_t bytes, size_t heap_budget)
{
    return browser_engine_start_assets(html, bytes, heap_budget, NULL);
}
int browser_engine_start_assets(const char *html, size_t bytes, size_t heap_budget,
                                const BrowserEngineAssets *prepared)
{
    struct lws_context_creation_info info;
    if (context || parser || live_bytes) return -1;
    peak_bytes = failures = 0;
    error = BROWSER_ENGINE_ERROR_NONE; phase = BROWSER_ENGINE_IDLE;
    clipped = parse_complete = parse_failed = 0;
    scroll_y = 0; content_height = VIEW_HEIGHT;
    memset(&render_state, 0, sizeof(render_state));
    memset(&layout, 0, sizeof(layout));
    if (!html || !bytes || bytes > HTML_LIMIT || heap_budget < 24u * 1024u ||
        heap_budget > HEAP_LIMIT) { fail(BROWSER_ENGINE_ERROR_INPUT); return -1; }
    memset(&assets, 0, sizeof(assets));
    if (prepared) assets = *prepared;
    budget_bytes = heap_budget;
    document = (const uint8_t *)html; document_bytes = bytes; parsed_bytes = 0;
    deadline = now_us() + TIME_LIMIT_US;
    lws_set_log_level(LLL_ERR | LLL_WARN, private_log);
    lws_set_allocator(bounded_realloc);
    /* No explicit-vhost flag / Secure Streams policy, listening port, or event
     * service loop: this context only provides fonts and display-list storage. */
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.fd_limit_per_thread = 8;
    info.max_http_header_data = 4096;
    info.pt_serv_buf_size = 4096;
    context = lws_create_context(&info);
    if (!context || failures) { fail(BROWSER_ENGINE_ERROR_MEMORY); return -1; }
    if (lws_font_register(context, regular16, sizeof(regular16)) ||
        lws_font_register(context, regular20, sizeof(regular20)) ||
        lws_font_register(context, regular24, sizeof(regular24)) ||
        lws_font_register(context, regular32, sizeof(regular32)) ||
        lws_font_register(context, bold20, sizeof(bold20)) ||
        lws_font_register(context, bold32, sizeof(bold32))) {
        fail(BROWSER_ENGINE_ERROR_MEMORY); return -1;
    }
    render_state.ic = &surface; render_state.cx = context;
    render_state.retained = 1; render_state.viewport_h = VIEW_HEIGHT;
    lws_display_dl_init(&render_state.displaylist, NULL);
    layout.dl = &render_state.displaylist;
    layout.w = VIEW_WIDTH; layout.h = DOCUMENT_HEIGHT;
    parser = bounded_realloc(NULL, sizeof(*parser), "browser parser");
    if (!parser) { fail(BROWSER_ENGINE_ERROR_MEMORY); return -1; }
    memset(parser, 0, sizeof(*parser));
    if (lws_lhp_construct(parser, layout_callback, &layout, &surface)) {
        fail(BROWSER_ENGINE_ERROR_MEMORY); return -1;
    }
    parser_constructed = 1;
    parser->user1 = context; parser->ids = &render_state.ids;
    parser->viewport_h = VIEW_HEIGHT;
    /* Deliberately NULL: pinned lhp.c skips every asset before URL resolution
     * when base_url is absent, including absolute URLs, file:, data:, CSS
     * backgrounds and <link>. <base> does not change it; @import is skipped.
     * Links retain their raw href. Never replace this with the fetched URL. */
    parser->base_url = NULL;
    phase = BROWSER_ENGINE_LAYOUT;
    return 0;
}
enum BrowserEngineState browser_engine_service(void)
{
    if (phase != BROWSER_ENGINE_LAYOUT && phase != BROWSER_ENGINE_PAINTING)
        return phase;
    if (now_us() >= deadline) return fail(BROWSER_ENGINE_ERROR_TIMEOUT);
    if (phase == BROWSER_ENGINE_LAYOUT) {
        size_t offered = document_bytes - parsed_bytes;
        const uint8_t *p = document + parsed_bytes;
        size_t remaining;
        lws_stateful_ret_t r;
        if (offered > 512) offered = 512;
        remaining = offered;
        if (parsed_bytes + offered == document_bytes)
            parser->flags |= LHP_FLAG_DOCUMENT_END;
        r = lws_lhp_parse(parser, &p, &remaining);
        parsed_bytes += offered - remaining;
        if (failures) return fail(BROWSER_ENGINE_ERROR_MEMORY);
        if ((r & LWS_SRET_FATAL) || parse_failed || remaining ||
            (r & LWS_SRET_AWAIT_RETRY)) return fail(BROWSER_ENGINE_ERROR_LAYOUT);
        if (parse_complete) {
            if (!supported_depth()) return fail(BROWSER_ENGINE_ERROR_LAYOUT);
            measure_document();
            destroy_parser();
            render_state.line = bounded_realloc(NULL, LINE_BYTES, "browser RGB row");
            if (!render_state.line) return fail(BROWSER_ENGINE_ERROR_MEMORY);
            if (begin_paint(0)) return phase;
        } else if (parsed_bytes == document_bytes) return fail(BROWSER_ENGINE_ERROR_LAYOUT);
        return phase;
    }
    for (unsigned rows = 0; rows < 4; rows++) {
        lws_stateful_ret_t r;
        memset(render_state.line, 0xff, LINE_BYTES);
        r = lws_display_list_render_line(&render_state);
        if (failures) return fail(BROWSER_ENGINE_ERROR_MEMORY);
        if (r) return fail(BROWSER_ENGINE_ERROR_LAYOUT);
        memmove(render_state.line + BROWSER_PAGE_MARGIN * 6u,
                render_state.line, VIEW_WIDTH * 3u);
        memset(render_state.line, 0xff, BROWSER_PAGE_MARGIN * 6u);
        memset(render_state.line + LINE_BYTES - BROWSER_PAGE_MARGIN * 6u,
               0xff, BROWSER_PAGE_MARGIN * 6u);
        if (!browser_panel_line((unsigned)render_state.curr - scroll_y,
                                render_state.line, LINE_BYTES))
            return fail(BROWSER_ENGINE_ERROR_PANEL);
        render_state.curr++;
        if ((unsigned)render_state.curr == scroll_y + VIEW_HEIGHT) {
            if (!browser_panel_finish()) return fail(BROWSER_ENGINE_ERROR_PANEL);
            phase = BROWSER_ENGINE_READY; break;
        }
    }
    return phase;
}
void browser_engine_resume(void) { deadline = now_us() + TIME_LIMIT_US; }
void browser_engine_stop(void)
{
    cleanup(); phase = BROWSER_ENGINE_IDLE; error = BROWSER_ENGINE_ERROR_NONE;
    scroll_y = 0; content_height = VIEW_HEIGHT; clipped = 0;
}
int browser_engine_scroll(int absolute_y)
{
    unsigned offset;
    if (phase != BROWSER_ENGINE_READY && phase != BROWSER_ENGINE_PAINTING) return -1;
    offset = absolute_y < 0 ? 0u : (unsigned)absolute_y;
    if (offset > content_height - VIEW_HEIGHT) offset = content_height - VIEW_HEIGHT;
    return begin_paint(offset);
}
int browser_engine_link_at(unsigned x, unsigned y, char *out, size_t capacity)
{
    lws_box_t box;
    lws_dlo_hit_t *hit;
    size_t bytes;
    if (!out || !capacity) return -1;
    out[0] = '\0';
    if (phase != BROWSER_ENGINE_READY || x < BROWSER_PAGE_MARGIN * 2u ||
        x >= 480u - BROWSER_PAGE_MARGIN * 2u || y >= VIEW_HEIGHT) return 0;
    x -= BROWSER_PAGE_MARGIN * 2u;
    hit = lws_display_dl_hit_test(&render_state.displaylist, (int)x,
                                 (int)(y + scroll_y), &box);
    if (!hit || !hit->url) return 0;
    bytes = strlen(hit->url);
    if (bytes >= capacity) return -1;
    memcpy(out, hit->url, bytes + 1);
    return 1;
}
unsigned browser_engine_scroll_y(void) { return scroll_y; }
unsigned browser_engine_content_height(void) { return content_height; }
int browser_engine_clipped(void) { return clipped; }
enum BrowserEngineError browser_engine_error(void) { return error; }
size_t browser_engine_peak_bytes(void) { return peak_bytes; }
size_t browser_engine_live_bytes(void) { return live_bytes; }
size_t browser_engine_failures(void) { return failures; }
