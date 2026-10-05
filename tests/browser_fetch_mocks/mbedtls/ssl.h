#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr int MBEDTLS_SSL_IS_CLIENT=0,MBEDTLS_SSL_TRANSPORT_STREAM=0,MBEDTLS_SSL_PRESET_DEFAULT=0;
constexpr int MBEDTLS_SSL_VERIFY_REQUIRED=2,MBEDTLS_SSL_VERSION_TLS1_2=0x303;
constexpr int MBEDTLS_ERR_SSL_WANT_READ=-20,MBEDTLS_ERR_SSL_WANT_WRITE=-21,MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY=-22;
struct mbedtls_ssl_config {int auth=0,minimum=0;bool bundle=false;};
struct mbedtls_ssl_context {mbedtls_ssl_config* config=nullptr;int* ioFd=nullptr;char hostname[254]{};};
void mbedtls_ssl_init(mbedtls_ssl_context*);
void mbedtls_ssl_free(mbedtls_ssl_context*);
inline void mbedtls_ssl_config_init(mbedtls_ssl_config*){}
inline void mbedtls_ssl_config_free(mbedtls_ssl_config*){}
inline int mbedtls_ssl_config_defaults(mbedtls_ssl_config*,int,int,int){return 0;}
inline void mbedtls_ssl_conf_authmode(mbedtls_ssl_config* p,int n){p->auth=n;}
inline void mbedtls_ssl_conf_min_tls_version(mbedtls_ssl_config* p,int n){p->minimum=n;}
inline void mbedtls_ssl_conf_rng(mbedtls_ssl_config*,int(*)(void*,unsigned char*,size_t),void*){}
int mbedtls_ssl_setup(mbedtls_ssl_context*,const mbedtls_ssl_config*);
int mbedtls_ssl_set_hostname(mbedtls_ssl_context*,const char*);
void mbedtls_ssl_set_bio(mbedtls_ssl_context*,void*,int(*)(void*,const unsigned char*,size_t),int(*)(void*,unsigned char*,size_t),void*);
int mbedtls_ssl_handshake(mbedtls_ssl_context*);
uint32_t mbedtls_ssl_get_verify_result(const mbedtls_ssl_context*);
int mbedtls_ssl_write(mbedtls_ssl_context*,const unsigned char*,size_t);
int mbedtls_ssl_read(mbedtls_ssl_context*,unsigned char*,size_t);
