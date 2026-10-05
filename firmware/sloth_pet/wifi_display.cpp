#include "wifi_display.h"
#include "paired_wifi_store.h"
#include "wifi_networks.h"
#include "wifi_network_store.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <atomic>
#include <new>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

namespace sloth {
namespace wifi_display {
namespace {
constexpr size_t kRxBytes = 8192, kTxBytes = 2048;
constexpr uint32_t kHandshakeMs = 8000;
std::atomic<bool> running(false), cancelled(false);
portMUX_TYPE stateLock = portMUX_INITIALIZER_UNLOCKED;
Snapshot info{};
paired_wifi_store::Profile profile{};
bool provisioning = false;
bool mdnsStarted = false;
struct Queues {
  StaticStreamBuffer_t rxControl,txControl;
  uint8_t rxStorage[kRxBytes+1],txStorage[kTxBytes+1];
};
Queues* queues=nullptr;
StreamBufferHandle_t rxQueue = nullptr, txQueue = nullptr;

void setState(State state, Error error = Error::None) {
  portENTER_CRITICAL(&stateLock);
  info.state = state;
  info.error = error;
  portEXIT_CRITICAL(&stateLock);
}
void erase(void* bytes, size_t length) { mbedtls_platform_zeroize(bytes, length); }
void metrics(int tlsError = 0) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr);
  portENTER_CRITICAL(&stateLock);
  info.freeHeap = freeHeap;
  if (!info.minimumFreeHeap || freeHeap < info.minimumFreeHeap) info.minimumFreeHeap = freeHeap;
  info.stackFreeBytes = stackFreeBytes;
  if (tlsError) info.tlsError = tlsError;
  portEXIT_CRITICAL(&stateLock);
}
void hex(const uint8_t* bytes, size_t length, char* output) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < length; ++i) {
    output[2 * i] = digits[bytes[i] >> 4];
    output[2 * i + 1] = digits[bytes[i] & 15];
  }
  output[2 * length] = 0;
}
bool elapsed(uint32_t start, uint32_t limit) {
  return static_cast<uint32_t>(millis() - start) >= limit;
}
bool again(int result) {
  return result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE;
}
int socketSend(void* context, const unsigned char* data, size_t length) {
  const int result = send(*static_cast<int*>(context), data, length, 0);
  if (result >= 0) return result;
  if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    return MBEDTLS_ERR_SSL_WANT_WRITE;
  return MBEDTLS_ERR_NET_SEND_FAILED;
}
int socketReceive(void* context, unsigned char* data, size_t length) {
  const int result = recv(*static_cast<int*>(context), data, length, 0);
  const int savedError = errno;
  if (result > 0) {
    portENTER_CRITICAL(&stateLock);
    info.socketRxBytes += static_cast<uint32_t>(result);
    portEXIT_CRITICAL(&stateLock);
  }
  if (result >= 0) return result;
  if (savedError == EAGAIN || savedError == EWOULDBLOCK || savedError == EINTR) {
    portENTER_CRITICAL(&stateLock);
    ++info.socketWouldBlock;
    portEXIT_CRITICAL(&stateLock);
    return MBEDTLS_ERR_SSL_WANT_READ;
  }
  return MBEDTLS_ERR_NET_RECV_FAILED;
}
bool nonblocking(int socket) {
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

struct TLS {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_pk_context key;
  mbedtls_x509_crt certificate;
  mbedtls_ssl_config config;
  mbedtls_ssl_context ssl;
  TLS() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    mbedtls_pk_init(&key);
    mbedtls_x509_crt_init(&certificate);
    mbedtls_ssl_config_init(&config);
    mbedtls_ssl_init(&ssl);
  }
  ~TLS() {
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_x509_crt_free(&certificate);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
  }
  bool generateIdentity(paired_wifi_store::Profile& saved) {
    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                           mbedtls_ctr_drbg_random, &random) != 0) return false;
    uint8_t secret[32], serial[16], der[1024];
    if (mbedtls_ctr_drbg_random(&random, secret, sizeof(secret)) != 0 ||
        mbedtls_ctr_drbg_random(&random, serial, sizeof(serial)) != 0) {
      erase(secret, sizeof(secret));
      return false;
    }
    serial[0] = (serial[0] & 0x7f) | 1;
    hex(secret, sizeof(secret), saved.token);
    // The public ID is independent of the authentication secret and TLS key.
    uint8_t identifier[16];
    if (mbedtls_ctr_drbg_random(&random, identifier, sizeof(identifier)) != 0) {
      erase(secret, sizeof(secret));
      return false;
    }
    hex(identifier, sizeof(identifier), saved.deviceId);
    erase(secret, sizeof(secret));
    mbedtls_x509write_cert writer;
    mbedtls_x509write_crt_init(&writer);
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &key);
    int result = mbedtls_x509write_crt_set_subject_name(&writer, "CN=Moss Display");
    if (!result) result = mbedtls_x509write_crt_set_issuer_name(&writer, "CN=Moss Display");
    if (!result) result = mbedtls_x509write_crt_set_serial_raw(&writer, serial, sizeof(serial));
    // Authentication is exact DER pinning from USB, independent of wall clock.
    if (!result) result = mbedtls_x509write_crt_set_validity(&writer, "20200101000000", "20991231235959");
    if (!result) result = mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1);
    if (!result) result = mbedtls_x509write_crt_set_key_usage(&writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    if (!result) result = mbedtls_x509write_crt_der(&writer, der, sizeof(der), mbedtls_ctr_drbg_random, &random);
    mbedtls_x509write_crt_free(&writer);
    if (result <= 0) return false;
    const size_t size = static_cast<size_t>(result);
    const uint8_t* encoded = der + sizeof(der) - size;
    if (size > sizeof(saved.certificate)) return false;
    memcpy(saved.certificate, encoded, size);
    saved.certificateLength = static_cast<uint16_t>(size);
    result = mbedtls_pk_write_key_der(&key, saved.key, sizeof(saved.key));
    if (result <= 0) return false;
    saved.keyLength = static_cast<uint16_t>(result);
    memmove(saved.key, saved.key + sizeof(saved.key) - saved.keyLength, saved.keyLength);
    erase(saved.key + saved.keyLength, sizeof(saved.key) - saved.keyLength);
    return mbedtls_x509_crt_parse_der(&certificate, saved.certificate, saved.certificateLength) == 0;
  }
  bool prepare(paired_wifi_store::Profile& saved, char* fingerprint) {
    static const unsigned char personalization[] = "Moss paired display TLS";
    if (mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                            personalization, sizeof(personalization) - 1) != 0) return false;
    if (saved.keyLength) {
      if (mbedtls_pk_parse_key(&key, saved.key, saved.keyLength, nullptr, 0,
                              mbedtls_ctr_drbg_random, &random) != 0 ||
          !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECKEY) ||
          mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(key)) != MBEDTLS_ECP_DP_SECP256R1 ||
          mbedtls_x509_crt_parse_der(&certificate, saved.certificate, saved.certificateLength) != 0 ||
          mbedtls_pk_check_pair(&certificate.pk, &key, mbedtls_ctr_drbg_random, &random) != 0) return false;
    } else if (!generateIdentity(saved)) return false;
    uint8_t digest[32];
    if (mbedtls_sha256(saved.certificate, saved.certificateLength, digest, 0) != 0) return false;
    hex(digest, sizeof(digest), fingerprint);
    if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_SERVER,
          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) return false;
    static const int suites[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, 0};
    mbedtls_ssl_conf_ciphersuites(&config, suites);
    mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    // Client possession of USB-provisioned token is checked inside pinned TLS.
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_session_tickets(&config, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
    mbedtls_ssl_conf_renegotiation(&config, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
    return mbedtls_ssl_conf_own_cert(&config, &certificate, &key) == 0 &&
           mbedtls_ssl_setup(&ssl, &config) == 0;
  }
};

bool writeFully(TLS& tls, const uint8_t* bytes, size_t length, uint32_t limit) {
  const uint32_t start = millis();
  size_t sent = 0;
  while (!cancelled.load() && !elapsed(start, limit)) {
    const int count = mbedtls_ssl_write(&tls.ssl, bytes + sent, length - sent);
    if (count > 0) {
      sent += static_cast<size_t>(count);
      if (sent == length) return true;
    } else if (!again(count)) return false;
    vTaskDelay(1);
  }
  return false;
}
bool authenticate(TLS& tls, const char* token) {
  char expected[76], received[76]{};
  memcpy(expected, "MOSS AUTH ", 10);
  memcpy(expected + 10, token, 64);
  expected[74] = '\n';
  expected[75] = 0;
  const uint32_t start = millis();
  size_t used = 0;
  bool matched = false;
  while (!cancelled.load() && !elapsed(start, kHandshakeMs)) {
    const int count = mbedtls_ssl_read(&tls.ssl,
        reinterpret_cast<unsigned char*>(received) + used, 75 - used);
    if (count > 0) {
      used += static_cast<size_t>(count);
      if (used == 75) {
        uint8_t difference = 0;
        for (size_t i = 0; i < 75; ++i) difference |= expected[i] ^ received[i];
        matched = difference == 0;
        break;
      }
    } else if (!again(count)) break;
    vTaskDelay(1);
  }
  erase(expected, sizeof(expected));
  erase(received, sizeof(received));
  static const uint8_t accepted[] = "MOSS AUTH OK\n";
  return matched && writeFully(tls, accepted, sizeof(accepted) - 1, 2000);
}

bool networkConnected() {
  wifi_networks::tick();
  return wifi_networks::connected();
}

Error stream(TLS& tls, int client) {
  uint8_t receive[1024], transmit[512];
  size_t pending = 0, sent = 0;
  uint32_t writeStarted = 0, lastMetrics = 0;
  while (!cancelled.load()) {
    if (elapsed(lastMetrics, 1000)) { metrics(); lastMetrics = millis(); }
    // The main loop owns the session heartbeat lease. A still desktop may send
    // no pixel updates for minutes; the worker does not impose a pixel timeout.
    if (!networkConnected()) return Error::Disconnected;
    if (pending == 0) {
      pending = xStreamBufferReceive(txQueue, transmit, sizeof(transmit), 0);
      sent = 0;
      writeStarted = millis();
    }
    if (pending != 0) {
      const int count = mbedtls_ssl_write(&tls.ssl, transmit + sent, pending - sent);
      if (count > 0) {
        sent += static_cast<size_t>(count);
        portENTER_CRITICAL(&stateLock);
        info.sentBytes += static_cast<uint32_t>(count);
        portEXIT_CRITICAL(&stateLock);
        if (sent == pending) pending = 0;
      } else if (!again(count)) { metrics(count); return Error::Disconnected; }
      if (pending && elapsed(writeStarted, 3000)) return Error::Disconnected;
      if (pending) { vTaskDelay(1); continue; }
    }
    size_t space = xStreamBufferSpacesAvailable(rxQueue);
    if (space > sizeof(receive)) space = sizeof(receive);
    if (space) {
      const uint32_t readStarted = micros();
      const int count = mbedtls_ssl_read(&tls.ssl, receive, space);
      const uint32_t readMicros = static_cast<uint32_t>(micros() - readStarted);
      portENTER_CRITICAL(&stateLock);
      info.tlsReadMicros += readMicros;
      ++info.tlsReadCalls;
      portEXIT_CRITICAL(&stateLock);
      if (count > 0) {
        if (xStreamBufferSend(rxQueue, receive, count, 0) != static_cast<size_t>(count))
          return Error::Memory;
        const uint32_t queuedBytes = static_cast<uint32_t>(xStreamBufferBytesAvailable(rxQueue));
        portENTER_CRITICAL(&stateLock);
        info.receivedBytes += static_cast<uint32_t>(count);
        if (queuedBytes > info.rxQueueHighWaterBytes) info.rxQueueHighWaterBytes = queuedBytes;
        portEXIT_CRITICAL(&stateLock);
        continue; // Drain queued TLS records without adding a tick per kilobyte.
      }
      if (!again(count)) { metrics(count); return Error::Disconnected; }
      if (count == MBEDTLS_ERR_SSL_WANT_READ) {
        // WANT_READ means mbedTLS has consumed its buffered records. Wait for
        // actual socket readiness instead of polling every millisecond. Only
        // this worker waits; buttons run independently in the main loop.
        if (cancelled.load()) return Error::None;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(client, &readable);
        timeval timeout = {0, 10000}; // Bound cancellation/queue checks to 10 ms.
        const int ready = select(client + 1, &readable, nullptr, nullptr, &timeout);
        if (ready < 0 && errno != EINTR) return Error::Disconnected;
        // EOF also reports readability; the next TLS read handles closure.
        continue;
      }
    } else {
      portENTER_CRITICAL(&stateLock);
      ++info.rxQueueFullTicks;
      portEXIT_CRITICAL(&stateLock);
    }
    vTaskDelay(1);
  }
  return Error::None;
}

Error runSession() {
  // The display worker exclusively owns the shared station service until exit.
  if (!wifi_networks::connect(profile.ssid, profile.password, false)) return Error::Join;
  while (!cancelled.load()) {
    wifi_networks::tick();
    const wifi_networks::State state = wifi_networks::snapshot().state;
    if (state == wifi_networks::State::Connected) break;
    if (state != wifi_networks::State::Joining) return Error::Join;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (cancelled.load()) return Error::None;
  metrics();
  setState(State::Preparing);
  TLS tls;
  char fingerprint[65]{}, token[65]{};
  if (!tls.prepare(profile, fingerprint)) {
    metrics();
    erase(token, sizeof(token));
    return Error::Certificate;
  }
  if (cancelled.load()) return Error::None;
  // Commit a complete profile before exposing its pairing identity to the Mac.
  if (provisioning && !paired_wifi_store::save(profile)) return Error::Storage;
  // The independent canonical network record is also the migration boundary:
  // Settings and Display subsequently use it rather than legacy profile fields.
  wifi_network_store::Credentials network{};
  memcpy(network.ssid, profile.ssid, sizeof(network.ssid));
  memcpy(network.password, profile.password, sizeof(network.password));
  const bool networkSaved = wifi_network_store::save(network);
  wifi_network_store::clear(network);
  if (!networkSaved) return Error::Storage;
  memcpy(token, profile.token, sizeof(token));
  char deviceId[33];
  memcpy(deviceId, profile.deviceId, sizeof(deviceId));
  paired_wifi_store::clear(profile);
  if (cancelled.load()) { erase(token, sizeof(token)); return Error::None; }
  metrics();
  int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener < 0) { erase(token, sizeof(token)); return Error::Socket; }
  struct sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = 0; // Discovery advertises this session's ephemeral port.
  socklen_t addressSize = sizeof(address);
  if (!nonblocking(listener) || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(listener, 1) != 0 || getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressSize) != 0) {
    close(listener);
    erase(token, sizeof(token));
    return Error::Socket;
  }
  char hostname[38], nonceText[17];
  snprintf(hostname, sizeof(hostname), "moss-%s", deviceId);
  const uint64_t nonce = snapshot().nonce;
  snprintf(nonceText, sizeof(nonceText), "%016llx", static_cast<unsigned long long>(nonce));
  // Discovery supplies only routing hints. The Mac still requires its saved
  // certificate pin and token; neither secret is ever published through mDNS.
  if (mdns_init() != ESP_OK) { close(listener); erase(token, sizeof(token)); return Error::Discovery; }
  mdnsStarted = true;
  mdns_txt_item_t txt[] = {{"v", "1"}, {"id", deviceId}, {"nonce", nonceText}};
  if (mdns_hostname_set(hostname) != ESP_OK ||
      mdns_service_add("Moss Display", "_moss-display", "_tcp", ntohs(address.sin_port), txt, 3) != ESP_OK) {
    close(listener);
    erase(token, sizeof(token));
    return Error::Discovery;
  }
  const wifi_networks::Snapshot networkInfo = wifi_networks::snapshot();
  portENTER_CRITICAL(&stateLock);
  memcpy(info.fingerprint, fingerprint, sizeof(fingerprint));
  memcpy(info.token, token, sizeof(token));
  memcpy(info.deviceId, deviceId, sizeof(deviceId));
  memcpy(info.address, networkInfo.address, sizeof(info.address));
  info.port = ntohs(address.sin_port);
  info.rssi = networkInfo.rssi;
  info.state = State::Listening;
  portEXIT_CRITICAL(&stateLock);
  int client = -1;
  while (!cancelled.load() && networkConnected()) {
    client = accept(listener, nullptr, nullptr);
    if (client >= 0) break;
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  close(listener);
  if (client < 0) {
    erase(token, sizeof(token));
    return cancelled.load() ? Error::None : Error::Socket;
  }
  if (!nonblocking(client)) { close(client); erase(token, sizeof(token)); return Error::Socket; }
  const int enabled = 1;
  setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
  mbedtls_ssl_set_bio(&tls.ssl, &client, socketSend, socketReceive, nullptr);
  setState(State::Authenticating);
  const uint32_t handshakeStarted = millis();
  int result = MBEDTLS_ERR_SSL_WANT_READ;
  while (!cancelled.load() && !elapsed(handshakeStarted, kHandshakeMs)) {
    result = mbedtls_ssl_handshake(&tls.ssl);
    if (!again(result)) break;
    vTaskDelay(1);
  }
  Error error = Error::Handshake;
  metrics(result);
  if (result == 0 && !cancelled.load()) {
    if (authenticate(tls, token)) {
      // The authenticated socket no longer needs discovery. Release mDNS's
      // task/stack and service buffers before desktop/audio traffic needs RAM.
      // A subsequent user-entered session starts discovery again.
      if (mdnsStarted) { mdns_free(); mdnsStarted = false; }
      metrics();
      if (!cancelled.load()) {
        if (Serial) {
          const Snapshot ready = snapshot();
          Serial.printf("WIFI TLS heap=%lu min_heap=%lu stack_free=%lu\n",
              static_cast<unsigned long>(ready.freeHeap),
              static_cast<unsigned long>(ready.minimumFreeHeap),
              static_cast<unsigned long>(ready.stackFreeBytes));
        }
        if (!cancelled.load()) {
          setState(State::Connected);
          error = stream(tls, client);
        }
      }
    } else error = Error::Authentication;
  }
  erase(token, sizeof(token));
  shutdown(client, SHUT_RDWR);
  close(client);
  return cancelled.load() ? Error::None : error;
}

void worker(void*) {
  metrics();
  const Error result = runSession();
  if (mdnsStarted) { mdns_free(); mdnsStarted = false; }
  wifi_networks::stop();
  paired_wifi_store::clear(profile);
  // Main's API stops accessing queues after state transitions away from Connected.
  portENTER_CRITICAL(&stateLock);
  info.state = cancelled.load() ? State::Off : State::Failed;
  info.error = result;
  erase(info.token, sizeof(info.token));
  erase(info.fingerprint, sizeof(info.fingerprint));
  info.address[0] = 0;
  info.port = 0;
  portEXIT_CRITICAL(&stateLock);
  running.store(false);
  vTaskDelete(nullptr);
}
bool start(uint64_t nonce) {
  reap(); // Main-loop owner, after the previous worker has stopped using queues.
  queues=new(std::nothrow) Queues{};
  if(queues){
    rxQueue=xStreamBufferCreateStatic(sizeof(queues->rxStorage),1,queues->rxStorage,&queues->rxControl);
    txQueue=xStreamBufferCreateStatic(sizeof(queues->txStorage),1,queues->txStorage,&queues->txControl);
  }
  if (!rxQueue || !txQueue) { reap(); paired_wifi_store::clear(profile); setState(State::Failed, Error::Memory); return false; }
  // Only the main-loop owner clears queues, after the old worker has exited.
  // A worker-side clear could race read() after its Connected state check.
  xStreamBufferReset(rxQueue);
  xStreamBufferReset(txQueue);
  erase(queues->rxStorage, sizeof(queues->rxStorage));
  erase(queues->txStorage, sizeof(queues->txStorage));
  portENTER_CRITICAL(&stateLock);
  info = Snapshot{};
  info.nonce = nonce;
  info.state = State::Joining;
  portEXIT_CRITICAL(&stateLock);
  cancelled.store(false);
  running.store(true);
  if (xTaskCreate(worker, "MossWiFi", 10240, nullptr, 1, nullptr) != pdPASS) {
    running.store(false);
    reap();
    paired_wifi_store::clear(profile);
    setState(State::Failed, Error::Memory);
    return false;
  }
  return true;
}
}  // namespace

bool begin(const char* ssid, const char* password, uint64_t nonce) {
  if (running.load() || !ssid || !password || !nonce) return false;
  const size_t nameSize = strnlen(ssid, sizeof(profile.ssid));
  const size_t passwordSize = strnlen(password, sizeof(profile.password));
  if (!nameSize || nameSize > 32 || passwordSize > 64) return false;
  const paired_wifi_store::Result result = paired_wifi_store::load(profile);
  if (result != paired_wifi_store::Result::Ok && result != paired_wifi_store::Result::Missing) {
    setState(State::Failed, Error::Storage);
    return false;
  }
  // Updating the network preserves the existing Mac trust identity.
  erase(profile.ssid, sizeof(profile.ssid));
  erase(profile.password, sizeof(profile.password));
  memcpy(profile.ssid, ssid, nameSize + 1);
  memcpy(profile.password, password, passwordSize + 1);
  provisioning = true;
  return start(nonce);
}
bool beginSaved(uint64_t nonce) {
  if (running.load() || !nonce) return false;
  const paired_wifi_store::Result identity = paired_wifi_store::load(profile);
  if (identity != paired_wifi_store::Result::Ok && identity != paired_wifi_store::Result::Missing) {
    setState(State::Failed, Error::Storage);
    return false;
  }
  wifi_network_store::Credentials network{};
  if (wifi_network_store::load(network) != wifi_network_store::Result::Ok) {
    paired_wifi_store::clear(profile);
    wifi_network_store::clear(network);
    setState(State::Failed, Error::Storage);
    return false;
  }
  memcpy(profile.ssid, network.ssid, sizeof(profile.ssid));
  memcpy(profile.password, network.password, sizeof(profile.password));
  wifi_network_store::clear(network);
  // First Display use after a Settings-only connection creates the identity.
  // Existing identities retain their certificate, token and device ID.
  provisioning = identity == paired_wifi_store::Result::Missing;
  return start(nonce);
}
bool hasSaved() {
  return !running.load() && wifi_networks::hasSaved();
}
bool savedSsid(char* output, size_t capacity) {
  if (!output || !capacity) return false;
  output[0] = 0;
  return !running.load() && wifi_networks::savedSsid(output, capacity);
}
bool forgetSaved() {
  if (running.load()) return false; // Caller must stop and wait for cleanup.
  paired_wifi_store::clear(profile);
  return wifi_networks::forgetSaved();
}
void stop() {
  if (!running.load()) return;
  cancelled.store(true);
  setState(State::Stopping);
}
bool busy() { return running.load(); }
void reap() {
  // Only main frees these: a worker may stop immediately after main's Connected
  // check in read/write. It publishes running=false after its final queue use.
  if(running.load()||!queues)return;
  if(rxQueue)vStreamBufferDelete(rxQueue);
  if(txQueue)vStreamBufferDelete(txQueue);
  rxQueue=txQueue=nullptr;
  erase(queues,sizeof(*queues));delete queues;queues=nullptr;
}
size_t retainedBytes(){return queues?sizeof(Queues):0;}
Snapshot snapshot() {
  portENTER_CRITICAL(&stateLock);
  const Snapshot copy = info;
  portEXIT_CRITICAL(&stateLock);
  return copy;
}
size_t read(uint8_t* data, size_t capacity) {
  if (!data || !capacity || !rxQueue || cancelled.load() || snapshot().state != State::Connected) return 0;
  return xStreamBufferReceive(rxQueue, data, capacity, 0);
}
bool write(const uint8_t* data, size_t length) {
  if (!data || !length || !txQueue || cancelled.load() || snapshot().state != State::Connected ||
      xStreamBufferSpacesAvailable(txQueue) < length) return false;
  return xStreamBufferSend(txQueue, data, length, 0) == length;
}
}  // namespace wifi_display
}  // namespace sloth
