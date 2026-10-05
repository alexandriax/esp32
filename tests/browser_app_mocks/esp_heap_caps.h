#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT 1u
size_t heap_caps_get_free_size(uint32_t caps);
