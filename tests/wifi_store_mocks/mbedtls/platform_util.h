#pragma once
#include <stddef.h>
inline void mbedtls_platform_zeroize(void* p, size_t n) {
  volatile unsigned char* b = static_cast<volatile unsigned char*>(p);
  while (n--) *b++ = 0;
}
