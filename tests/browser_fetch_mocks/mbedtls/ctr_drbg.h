#pragma once
#include <stddef.h>
struct mbedtls_ctr_drbg_context {};
inline void mbedtls_ctr_drbg_init(mbedtls_ctr_drbg_context*){}
inline void mbedtls_ctr_drbg_free(mbedtls_ctr_drbg_context*){}
inline int mbedtls_ctr_drbg_seed(mbedtls_ctr_drbg_context*,int(*)(void*,unsigned char*,size_t),void*,const unsigned char*,size_t){return 0;}
inline int mbedtls_ctr_drbg_random(void*,unsigned char*,size_t){return 0;}
