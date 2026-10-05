#pragma once
#include <stddef.h>
struct mbedtls_entropy_context {};
inline void mbedtls_entropy_init(mbedtls_entropy_context*){}
inline void mbedtls_entropy_free(mbedtls_entropy_context*){}
inline int mbedtls_entropy_func(void*,unsigned char*,size_t){return 0;}
