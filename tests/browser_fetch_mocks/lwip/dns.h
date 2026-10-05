#pragma once
#include <stdint.h>
using err_t=int;
struct ip_addr_t {uint32_t address;};
using ip4_addr_t=ip_addr_t;
using dns_found_callback=void(*)(const char*,const ip_addr_t*,void*);
constexpr int ERR_OK=0,ERR_INPROGRESS=1,LWIP_DNS_ADDRTYPE_IPV4=0;
#define IP_IS_V4(ip) ((ip)!=nullptr)
#define ip_2_ip4(ip) (ip)
inline uint32_t ip4_addr_get_u32(const ip4_addr_t* p){return p->address;}
err_t dns_gethostbyname_addrtype(const char*,ip_addr_t*,dns_found_callback,void*,int);
