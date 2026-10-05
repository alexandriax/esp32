#include "browser_engine.h"
#include <libwebsockets.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
extern "C" size_t moss_probe_network_attempts;
static uint64_t fake_now = 1000;
extern "C" uint64_t browser_engine_test_now_us(void) { return fake_now; }
static std::vector<uint8_t> pixels(480 * BROWSER_VIEW_HEIGHT * 3);
static unsigned rows, finishes, begins, fail_row = 9999;
static bool fail_begin, fail_finish;
extern "C" int browser_panel_begin(void) {
    rows = 0; ++begins;
    return !fail_begin;
}
extern "C" int browser_panel_line(unsigned y, const uint8_t *rgb, size_t bytes) {
    assert(y == rows && bytes == 480 * 3);
    if (y == fail_row) return 0;
    std::memcpy(&pixels[y * bytes], rgb, bytes);
    ++rows;
    return 1;
}
extern "C" int browser_panel_finish(void) {
    assert(rows == BROWSER_VIEW_HEIGHT); ++finishes;
    return !fail_finish;
}
static const char page[] =
    "<html><head><style>body{margin:0;font-size:20px}div{height:350px}"
    "a{color:blue}</style></head><body>"
    "<div style='background:#ffaaaa'><a href='/one'>First link</a></div>"
    "<div style='background:#aaffaa'><a href='/two'>Second link</a></div>"
    "<div style='background:#aaaaff'>Third block</div></body></html>";
static BrowserEngineState finish() {
    for (unsigned i = 0; i < 10000; ++i) {
        const unsigned before = rows;
        const unsigned old_begins = begins;
        const BrowserEngineState s = browser_engine_service();
        if (begins == old_begins) assert(rows <= before + 4);
        if (s == BROWSER_ENGINE_READY || s == BROWSER_ENGINE_FAILED) return s;
    }
    assert(false && "bounded engine must terminate");
    return BROWSER_ENGINE_FAILED;
}
static void start(const char *html, size_t len) {
    assert(browser_engine_start(html, len, 128 * 1024) == 0);
    assert(finish() == BROWSER_ENGINE_READY);
    assert(browser_engine_error() == BROWSER_ENGINE_ERROR_NONE);
    assert(browser_engine_live_bytes() && browser_engine_peak_bytes() <= 128 * 1024);
    assert(moss_probe_network_attempts == 0);
}
static void stop() {
    browser_engine_stop();
    assert(browser_engine_service() == BROWSER_ENGINE_IDLE);
    assert(browser_engine_live_bytes() == 0);
    assert(moss_probe_network_attempts == 0);
}
static void link(unsigned x, unsigned y, const char *expected) {
    char target[100];
    assert(browser_engine_link_at(x, y, target, sizeof(target)) == 1);
    assert(std::strcmp(target, expected) == 0);
}
int main() {
    /* The renderer's private LWS build must not allocate OS wakeup pipes.
     * On the target these are UDP sockets that also retain thread-local lwIP
     * state. Verify the public wake-fd API and null-pipe cancel/destroy paths. */
    lws_set_log_level(0, nullptr);
    for (unsigned i = 0; i < 3; ++i) {
        lws_context_creation_info info{};
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.fd_limit_per_thread = 8;
        lws_context *context = lws_create_context(&info);
        assert(context && lws_service_wake_fd(context, 0) < 0);
        lws_cancel_service(context);
        lws_context_destroy(context);
        assert(moss_probe_network_attempts == 0);
    }
    start(page, sizeof(page) - 1);
    assert(browser_engine_content_height() == 1050 && !browser_engine_clipped());
    assert(rows == BROWSER_VIEW_HEIGHT);
    link(15, 10, "/one");
    const std::vector<uint8_t> first = pixels;
    assert(first[3 * (100 * 480 + 300)] == 255);
    assert(first[3 * (100 * 480 + 300) + 1] == 170);
    /* Text must alter pixels inside its line, beyond the CSS backgrounds. */
    unsigned dark = 0;
    for (unsigned y = 0; y < 25; ++y)
        for (unsigned x = 0; x < 90; ++x)
            if (pixels[3 * (y * 480 + x)] < 100) ++dark;
    assert(dark > 30);
    const size_t first_peak = browser_engine_peak_bytes();
    assert(browser_engine_start(page, sizeof(page) - 1, 128 * 1024) == -1);
    assert(browser_engine_service() == BROWSER_ENGINE_READY);
    assert(browser_engine_scroll(350) == 0);
    assert(finish() == BROWSER_ENGINE_READY);
    assert(pixels != first); link(15, 10, "/two");
    char target[100];
    assert(browser_engine_link_at(15, 360, target, sizeof(target)) == 0);
    assert(browser_engine_scroll(-42) == 0);
    assert(finish() == BROWSER_ENGINE_READY && pixels == first);
    link(15, 10, "/one");
    const unsigned before_repaint = begins;
    assert(browser_engine_scroll(0) == 0 && begins == before_repaint + 1);
    assert(finish() == BROWSER_ENGINE_READY && pixels == first);
    const size_t stable_heap = browser_engine_live_bytes();
    for (unsigned i = 0; i < 10; ++i) {
        assert(browser_engine_scroll(350) == 0);
        assert(finish() == BROWSER_ENGINE_READY);
        assert(browser_engine_scroll(0) == 0);
        assert(finish() == BROWSER_ENGINE_READY);
        assert(browser_engine_live_bytes() == stable_heap && pixels == first);
    }
    assert(browser_engine_scroll(1000000) == 0);
    assert(browser_engine_scroll_y() == 1050-BROWSER_VIEW_HEIGHT);
    browser_engine_service();
    assert(browser_engine_scroll(0) == 0); // interrupt a partial paint
    assert(finish() == BROWSER_ENGINE_READY && pixels == first);
    char tiny[3] = {'a','b','c'};
    assert(browser_engine_link_at(15, 10, tiny, sizeof(tiny)) == -1);
    assert(tiny[0] == 0 && tiny[1] == 'b' && tiny[2] == 'c');
    assert(browser_engine_link_at(480, 10, target, sizeof(target)) == 0);
    stop();

    /* All subresources are fenced even when the page supplies a <base> tag.
     * The host socket/DNS guard fails any accidental network attempt. */
    const char assets[] =
        "<html><head><base href='https://example.invalid/'>"
        "<link rel='stylesheet' href='file:///etc/passwd'>"
        "<link rel='stylesheet' href='HTTPS://example.invalid/a.css'>"
        "<style>@import url('https://example.invalid/b.css');"
        "body{background-image:url(https://example.invalid/c.png)}</style>"
        "</head><body><p>Assets blocked, text rendered.</p>"
        "<img src='https://example.invalid/a.png'><img src='HTTP://example.invalid/b.png'>"
        "<img src='//example.invalid/c.png'><img src='/local.png'>"
        "<img src='relative.png'><img src='file:///etc/passwd'>"
        "<img src='data:image/png;base64,eA=='><img src='ftp://example.invalid/a.png'>"
        "<iframe src='https://example.invalid/'></iframe>"
        "<script>location.href='https://example.invalid/';</script></body></html>";
    start(assets, sizeof(assets) - 1); stop();
    const char malformed[] = "<html><body><div><p>Malformed EOF with <a href='/next'>link";
    start(malformed, sizeof(malformed) - 1); stop();
    const char tall[] = "<html><body style='margin:0'><div style='height:9000px;background:red'>Tall</div><p>Below limit</p></body></html>";
    start(tall, sizeof(tall) - 1);
    assert(browser_engine_content_height() == 8192 && browser_engine_clipped());
    assert(browser_engine_scroll(99999) == 0 && browser_engine_scroll_y() == 8192-BROWSER_VIEW_HEIGHT);
    assert(finish() == BROWSER_ENGINE_READY); stop();

    std::string deep = "<html><body>";
    for (unsigned i = 0; i < 24; ++i) deep += "<div>";
    deep += "Deep text";
    for (unsigned i = 0; i < 24; ++i) deep += "</div>";
    deep += "</body></html>";
    assert(browser_engine_start(deep.data(), deep.size(), 128 * 1024) == 0);
    assert(finish() == BROWSER_ENGINE_FAILED);
    assert(browser_engine_error() == BROWSER_ENGINE_ERROR_LAYOUT);
    assert(browser_engine_live_bytes() == 0); stop();

    /* Cancellation at each phase and all three panel failure boundaries. */
    assert(browser_engine_start(page, sizeof(page) - 1, 128 * 1024) == 0); stop();
    assert(browser_engine_start(page, sizeof(page) - 1, 128 * 1024) == 0);
    assert(browser_engine_service() == BROWSER_ENGINE_PAINTING); stop();
    for (unsigned which = 0; which < 3; ++which) {
        fail_begin = which == 0; fail_row = which == 1 ? 32 : 9999;
        fail_finish = which == 2;
        assert(browser_engine_start(page, sizeof(page) - 1, 128 * 1024) == 0);
        assert(finish() == BROWSER_ENGINE_FAILED);
        assert(browser_engine_error() == BROWSER_ENGINE_ERROR_PANEL);
        assert(browser_engine_live_bytes() == 0);
        fail_begin = fail_finish = false; fail_row = 9999; stop();
    }
    assert(browser_engine_start(page, sizeof(page) - 1, 128 * 1024) == 0);
    fake_now += 16000000;
    assert(browser_engine_service() == BROWSER_ENGINE_FAILED);
    assert(browser_engine_error() == BROWSER_ENGINE_ERROR_TIMEOUT);
    assert(browser_engine_live_bytes() == 0); stop();
    start(page, sizeof(page) - 1);
    assert(browser_engine_scroll(10) == 0);
    fake_now += 16000000;
    assert(browser_engine_service() == BROWSER_ENGINE_FAILED);
    assert(browser_engine_error() == BROWSER_ENGINE_ERROR_TIMEOUT); stop();
    std::string large(65537, 'x');
    assert(browser_engine_start(large.data(), large.size(), 128 * 1024) == -1);
    assert(browser_engine_error() == BROWSER_ENGINE_ERROR_INPUT); stop();
    assert(browser_engine_start(nullptr, 0, 128 * 1024) == -1); stop();
    assert(browser_engine_start(page, sizeof(page) - 1, 1024) == -1); stop();

    /* Exercise real allocation failures at many distinct construction, parse,
     * layout and render points, then require a clean successful restart. */
    std::string crowded = "<html><body>";
    for (unsigned i = 0; i < 160; ++i)
        crowded += "<p style='padding:4px;color:#123456'><a href='/item'>Item text</a></p>";
    crowded += "</body></html>";
    unsigned oom_cases = 0;
    for (size_t cap = 24 * 1024; cap <= 128 * 1024; cap += 1024) {
        const int accepted = browser_engine_start(crowded.data(), crowded.size(), cap);
        const BrowserEngineState s = accepted == 0 ? finish() : browser_engine_service();
        if (s == BROWSER_ENGINE_FAILED) {
            assert(browser_engine_error() == BROWSER_ENGINE_ERROR_MEMORY);
            assert(browser_engine_failures() > 0);
            assert(browser_engine_live_bytes() == 0); ++oom_cases;
        } else assert(s == BROWSER_ENGINE_READY);
        stop();
    }
    assert(oom_cases > 10);

    /* Glyph arenas are allocated lazily while painting. A document can fit
     * during layout but exhaust the same quota on its first visible page. */
    std::string glyph_page = "<html><body><p>";
    for (unsigned i = 0; i < 16; ++i)
        glyph_page += "An arena owns the glyphs for this visible line. ";
    glyph_page += "</p></body></html>";
    unsigned paint_oom_cases = 0;
    for (size_t cap = 32 * 1024; cap <= 128 * 1024; cap += 2048) {
        const int accepted = browser_engine_start(glyph_page.data(), glyph_page.size(), cap);
        BrowserEngineState s = accepted == 0 ? BROWSER_ENGINE_LAYOUT : browser_engine_service();
        for (unsigned step = 0; s == BROWSER_ENGINE_LAYOUT || s == BROWSER_ENGINE_PAINTING; ++step) {
            assert(step < 10000);
            const bool painting = s == BROWSER_ENGINE_PAINTING;
            s = browser_engine_service();
            assert(browser_engine_peak_bytes() <= cap);
            if (painting && s == BROWSER_ENGINE_FAILED) ++paint_oom_cases;
        }
        if (s == BROWSER_ENGINE_FAILED) {
            assert(browser_engine_error() == BROWSER_ENGINE_ERROR_MEMORY);
            assert(browser_engine_failures() > 0 && browser_engine_live_bytes() == 0);
        } else assert(s == BROWSER_ENGINE_READY);
        stop();
    }
    assert(paint_oom_cases > 0);
    start(page, sizeof(page) - 1); stop();
    std::printf("browser engine: retained 1050px layout, 400-row scroll/link/pixel checks, "
                "%u OOM quotas, %u paint OOM quotas, cancellation, timeout and asset fence passed; "
                "initial peak=%zu bytes; network attempts=%zu\n",
                oom_cases, paint_oom_cases, first_peak, moss_probe_network_attempts);
}
