#pragma once
#include <stdint.h>
struct in_addr {uint32_t s_addr;};
struct sockaddr_in {uint16_t sin_family,sin_port;in_addr sin_addr;};
#ifndef htons
inline uint16_t htons(uint16_t n){return n;}
#endif
