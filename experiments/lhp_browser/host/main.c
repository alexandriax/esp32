/* Offline LHP experiment, deliberately outside the production Arduino sketch.
 * The scanline setup follows upstream api-test-lhp-dlo/main.c (CC0-1.0).
 * This measures native requested LWS heap, not ESP32 heap or TLS costs.
 */
#include <libwebsockets.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIDTH 480
#define HEIGHT 480
#define LINE_BYTES (WIDTH * 3)
#define MAX_INPUT_BYTES (64 * 1024)
#define DEFAULT_HEAP_LIMIT (200 * 1024)

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

/* Preserve malloc alignment; allocator bookkeeping is excluded from the quota. */
typedef union allocation_header {
    max_align_t alignment;
    size_t bytes;
} allocation_header;
static size_t live_bytes, peak_bytes, allocation_limit = DEFAULT_HEAP_LIMIT;
static size_t allocation_failures, largest_allocation;

static void *tracked_realloc(void *ptr, size_t bytes, const char *reason)
{
    allocation_header *old = ptr ? (allocation_header *)ptr - 1 : NULL;
    size_t previous = old ? old->bytes : 0;
    allocation_header *next;
    (void)reason;
    if (!bytes) {
        live_bytes -= previous;
        free(old);
        return NULL;
    }
    if (bytes > SIZE_MAX - sizeof(*next) ||
        bytes > allocation_limit || live_bytes - previous > allocation_limit - bytes) {
        allocation_failures++;
        return NULL;
    }
    next = realloc(old, sizeof(*next) + bytes);
    if (!next) {
        allocation_failures++;
        return NULL;
    }
    next->bytes = bytes;
    live_bytes = live_bytes - previous + bytes;
    if (live_bytes > peak_bytes)
        peak_bytes = live_bytes;
    if (bytes > largest_allocation)
        largest_allocation = bytes;
    return next + 1;
}

static struct lws_context *context;
static lws_display_render_state_t state;
static lws_sorted_usec_list_t deadline;
static lws_surface_info_t surface = {
    .wh_px = {{WIDTH, 0}, {HEIGHT, 0}},
    .wh_mm = {{39, 0}, {39, 0}},
    .type = LWSSURF_TRUECOLOR32,
    .greyscale = 0
};
static FILE *bmp_file, *layout_file;
static int finished, succeeded, timed_out;
static size_t nodes, links, text_nodes;
static double content_bottom;
extern size_t moss_probe_network_attempts;
static double start_ms, layout_ms;

static double monotonic_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return 0;
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
}

static void escaped(FILE *out, const char *text, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') fputs("\\n", out);
        else if (c == '\r') fputs("\\r", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c == '"' || c == '\\') { fputc('\\', out); fputc(c, out); }
        else if (c >= 32) fputc(c, out);
    }
}

static void dump_list(lws_dll2_owner_t *owner, unsigned depth, double parent_y)
{
    for (lws_dll2_t *item = lws_dll2_get_head(owner); item;
         item = lws_dll2_get_next(item)) {
        lws_dlo_t *dlo = lws_container_of(item, lws_dlo_t, list);
        const char *kind = "rect";
        const char *content = NULL;
        size_t length = 0;
        char x[32], y[32], w[32], h[32];
        nodes++;
        if (dlo->_destroy == lws_display_dlo_text_destroy) {
            lws_dlo_text_t *text = lws_container_of(dlo, lws_dlo_text_t, dlo);
            kind = "text"; content = text->text; length = text->text_len; text_nodes++;
        } else if (dlo->render == lws_display_render_hit) {
            lws_dlo_hit_t *hit = lws_container_of(dlo, lws_dlo_hit_t, dlo);
            kind = "link"; content = hit->url;
            length = content ? strlen(content) : 0; links++;
        } else if (dlo->_destroy == lws_display_dlo_png_destroy) kind = "png";
        else if (dlo->_destroy == lws_display_dlo_jpeg_destroy) kind = "jpeg";
        lws_fx_string(&dlo->box.x, x, sizeof(x));
        lws_fx_string(&dlo->box.y, y, sizeof(y));
        lws_fx_string(&dlo->box.w, w, sizeof(w));
        lws_fx_string(&dlo->box.h, h, sizeof(h));
        double top = parent_y + strtod(y, NULL);
        double bottom = top + strtod(h, NULL);
        if (bottom > content_bottom) content_bottom = bottom;
        fprintf(layout_file, "%*s%s x=%s y=%s w=%s h=%s rgba=%08x",
                (int)(depth * 2), "", kind, x, y, w, h, (unsigned)dlo->dc);
        if (content) {
            fputs(" \"", layout_file); escaped(layout_file, content, length);
            fputc('"', layout_file);
        }
        fputc('\n', layout_file);
        dump_list(&dlo->children, depth + 1, top);
    }
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static int bmp_header(void)
{
    uint8_t h[54] = {0};
    h[0] = 'B'; h[1] = 'M'; put32(h + 2, 54 + LINE_BYTES * HEIGHT);
    put32(h + 10, 54); put32(h + 14, 40); put32(h + 18, WIDTH);
    put32(h + 22, (uint32_t)-HEIGHT); h[26] = 1; h[28] = 24;
    return fwrite(h, 1, sizeof(h), bmp_file) == sizeof(h) ? 0 : -1;
}

static void timeout_callback(lws_sorted_usec_list_t *sul)
{
    (void)sul;
    timed_out = 1;
    finished = 1;
    lws_default_loop_exit(context);
}

static void render(lws_sorted_usec_list_t *sul)
{
    lws_display_render_state_t *rs = lws_container_of(sul, lws_display_render_state_t, sul);
    if (rs->html == 1 || finished) return;
    if (!rs->line) {
        layout_ms = monotonic_ms() - start_ms;
        lws_display_get_ids_boxes(rs);
        dump_list(&rs->displaylist.dl, 0, 0);
        if (ferror(layout_file)) { finished = 1; lws_default_loop_exit(context); return; }
        rs->line = tracked_realloc(NULL, LINE_BYTES, "host RGB scanline");
        if (!rs->line || bmp_header()) { finished = 1; lws_default_loop_exit(context); return; }
        memset(rs->line, 0xff, LINE_BYTES);
        rs->curr = 0;
    }
    /* Yield regularly so a runaway renderer cannot starve the deadline. */
    for (unsigned budget = 0; budget < 4 && rs->curr < HEIGHT; budget++) {
        lws_stateful_ret_t result = lws_display_list_render_line(rs);
        if (result) {
            lws_sul_schedule(context, 0, &rs->sul, render, LWS_US_PER_MS);
            return;
        }
        /* BMP stores BGR; conversion is in-place after this line is complete. */
        uint8_t *rgb = (uint8_t *)rs->line;
        for (unsigned x = 0; x < WIDTH; x++) {
            uint8_t r = rgb[x * 3]; rgb[x * 3] = rgb[x * 3 + 2]; rgb[x * 3 + 2] = r;
        }
        if (fwrite(rgb, 1, LINE_BYTES, bmp_file) != LINE_BYTES) {
            finished = 1;
            lws_default_loop_exit(context);
            return;
        }
        rs->curr++;
        memset(rs->line, 0xff, LINE_BYTES);
    }
    if (rs->curr == HEIGHT) {
        succeeded = 1; finished = 1; lws_default_loop_exit(context);
    } else lws_sul_schedule(context, 0, &rs->sul, render, 1);
}

int main(int argc, char **argv)
{
    struct lws_context_creation_info info;
    lws_lhp_filter_t filter = {.block_rules = "http://\nhttps://\n"};
    char url[4096];
    size_t baseline = 0;
    FILE *input = NULL;
    long input_bytes = 0;
    int result = 1;
    double elapsed = 0;
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s /absolute/input.html output.bmp layout.txt metrics.json [heap-limit-bytes]\n", argv[0]);
        return 2;
    }
    if (argv[1][0] != '/' || strstr(argv[1], "://") ||
        snprintf(url, sizeof(url), "file://%s", argv[1]) >= (int)sizeof(url)) {
        fputs("Only absolute local file paths are accepted.\n", stderr);
        return 2;
    }
    if (argc == 6) {
        char *end;
        unsigned long value = strtoul(argv[5], &end, 10);
        if (!*argv[5] || *end || value < 32768 || value > 4 * 1024 * 1024) return 2;
        allocation_limit = (size_t)value;
    }
    input = fopen(argv[1], "rb");
    if (!input || fseek(input, 0, SEEK_END) || (input_bytes = ftell(input)) <= 0 ||
        input_bytes > MAX_INPUT_BYTES) {
        if (input) fclose(input);
        fputs("Input must be a nonempty local HTML fixture of at most 64 KiB.\n", stderr);
        return 2;
    }
    fclose(input);
    bmp_file = fopen(argv[2], "wb"); layout_file = fopen(argv[3], "w");
    if (!bmp_file || !layout_file) goto cleanup;
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
    lws_set_allocator(tracked_realloc);
    lws_context_info_defaults(&info, NULL);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.fd_limit_per_thread = 8;
    info.max_http_header_data = 4096;
    info.pt_serv_buf_size = 4096;
    context = lws_create_context(&info);
    if (!context) goto cleanup;
    lws_font_register(context, regular16, sizeof(regular16));
    lws_font_register(context, regular20, sizeof(regular20));
    lws_font_register(context, regular24, sizeof(regular24));
    lws_font_register(context, regular32, sizeof(regular32));
    lws_font_register(context, bold20, sizeof(bold20));
    lws_font_register(context, bold32, sizeof(bold32));
    baseline = live_bytes;
    state.ic = &surface;
    start_ms = monotonic_ms();
    lws_sul_schedule(context, 0, &deadline, timeout_callback, 5 * LWS_US_PER_SEC);
    if (lws_lhp_ss_browse_filter(context, &state, url, render, &filter)) goto cleanup;
    while (!finished && lws_service(context, 0) >= 0) {}
    elapsed = monotonic_ms() - start_ms;
    result = succeeded && !allocation_failures && !moss_probe_network_attempts ? 0 : 1;
cleanup:
    if (context) {
        lws_sul_cancel(&deadline);
        lws_lhp_ss_cancel(&state);
        lws_sul_cancel(&state.sul);
        tracked_realloc(state.line, 0, "host RGB scanline"); state.line = NULL;
        lws_display_list_destroy(context, &state.displaylist);
        lws_context_destroy(context);
    }
    if (bmp_file && fclose(bmp_file)) result = 1;
    if (layout_file && fclose(layout_file)) result = 1;
    FILE *metrics = fopen(argv[4], "w");
    if (!metrics) return 1;
    fprintf(metrics, "{\n  \"success\": %s,\n  \"timed_out\": %s,\n"
            "  \"width\": %d, \"height\": %d, \"rendered_rows\": %d,\n"
            "  \"input_bytes\": %ld,\n  \"elapsed_ms\": %.3f, \"layout_ms\": %.3f,\n"
            "  \"lws_requested_heap_baseline_bytes\": %zu,\n"
            "  \"lws_requested_heap_peak_bytes\": %zu,\n"
            "  \"lws_requested_heap_limit_bytes\": %zu,\n"
            "  \"lws_requested_heap_after_cleanup_bytes\": %zu,\n"
            "  \"largest_requested_allocation_bytes\": %zu,\n"
            "  \"allocation_failures\": %zu,\n"
            "  \"network_attempts_blocked\": %zu, \"content_bottom_px\": %.2f,\n"
            "  \"scanline_bytes\": %d,\n  \"layout_nodes\": %zu, \"text_nodes\": %zu, \"link_regions\": %zu\n}\n",
            result ? "false" : "true", timed_out ? "true" : "false", WIDTH, HEIGHT,
            state.curr, input_bytes, elapsed, layout_ms, baseline, peak_bytes,
            allocation_limit, live_bytes, largest_allocation, allocation_failures,
            moss_probe_network_attempts, content_bottom, LINE_BYTES, nodes, text_nodes, links);
    if (fclose(metrics)) return 1;
    return result;
}
