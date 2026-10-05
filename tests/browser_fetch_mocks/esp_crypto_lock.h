#pragma once
#define SOC_SHA_SUPPORTED 1
#define SOC_MPI_SUPPORTED 1
#define SOC_ECC_SUPPORTED 1
void esp_crypto_sha_aes_lock_acquire();
void esp_crypto_sha_aes_lock_release();
void esp_crypto_mpi_lock_acquire();
void esp_crypto_mpi_lock_release();
void esp_crypto_ecc_lock_acquire();
void esp_crypto_ecc_lock_release();
