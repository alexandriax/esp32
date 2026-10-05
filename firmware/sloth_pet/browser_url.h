#pragma once
#include <stddef.h>
#include <stdint.h>
namespace sloth { namespace browser_url {
constexpr size_t kCapacity=512; // Including the terminating NUL.
struct Parts { bool secure; uint16_t port; char host[254]; char target[kCapacity]; };
// HTTP(S), DNS/IPv4 hosts, optional port, escaped ASCII path/query. Userinfo,
// IPv6 literals, controls, backslashes and unsupported schemes fail closed.
bool parse(const char* absolute,Parts& parts);
bool normalize(const char* input,char output[kCapacity]); // Defaults to HTTPS.
bool resolve(const char* base,const char* reference,char output[kCapacity]);
} }
