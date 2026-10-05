#pragma once
#include "mbedtls/ssl.h"
constexpr int ESP_OK=0;
int esp_crt_bundle_attach(void*);
void esp_crt_bundle_detach(mbedtls_ssl_config*);
