#include <libwebsockets.h>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Allocator-owned headers make a raw free visible to ASan as well as checking
// that lwsac charges chunk capacity (not only used payload) to the budget.
union Header { max_align_t alignment; size_t bytes; };
static size_t live, limit, allocations, rejections;
static void *bounded(void *ptr, size_t size, const char *) {
    Header *old = ptr ? static_cast<Header *>(ptr) - 1 : nullptr;
    const size_t prior = old ? old->bytes : 0;
    if (!size) { live -= prior; std::free(old); return nullptr; }
    if (size > limit || live - prior > limit - size) {
        ++rejections; return nullptr;
    }
    Header *next = static_cast<Header *>(std::realloc(old, sizeof(Header) + size));
    assert(next);
    next->bytes = size; live = live - prior + size; ++allocations;
    return next + 1;
}
int main() {
    lws_set_log_level(0, nullptr);
    lws_set_allocator(bounded);
    lwsac *arena = nullptr;
    limit = 1;
    assert(!lwsac_use(&arena, 1, 1024));
    assert(!arena && !live && rejections == 1);

    limit = 8192;
    unsigned char *first = static_cast<unsigned char *>(lwsac_use_zero(&arena, 32, 1024));
    assert(first && arena && allocations == 1);
    assert(live == lwsac_total_alloc(arena) && live >= 1024);
    for (unsigned i = 0; i < 32; ++i) assert(first[i] == 0);
    std::memset(first, 0x5a, 32);
    lwsac *original = arena;
    const size_t first_chunk = live;
    limit = first_chunk;
    // A rejected second chunk must preserve the head, contents, and accounting.
    assert(!lwsac_use(&arena, 2048, 2048));
    assert(arena == original && live == first_chunk && rejections == 2);
    for (unsigned i = 0; i < 32; ++i) assert(first[i] == 0x5a);
    // Reusing unused capacity is still permitted with no heap budget remaining.
    assert(lwsac_use_backfill(&arena, 16, 1024) && allocations == 1);
    limit = 8192;
    assert(lwsac_use(&arena, 2048, 2048) && allocations == 2);
    assert(live == lwsac_total_alloc(arena) && live > first_chunk);
    lwsac_free(&arena);
    assert(!arena && !live);

    // The alternative ownership API must release through that allocator too.
    assert(lwsac_use(&arena, 1, 64));
    lwsac_reference(arena);
    lwsac_detach(&arena);
    assert(arena && live);
    lwsac_unreference(&arena);
    assert(!arena && !live);
    std::puts("browser arena: all chunk bytes accounted, first/growth OOM preserved, free/detach clean");
}
