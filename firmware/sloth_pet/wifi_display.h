#ifndef SLOTH_WIFI_DISPLAY_H
#define SLOTH_WIFI_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

namespace sloth {
namespace wifi_display {

enum class State : uint8_t {
  Off, Joining, Preparing, Listening, Authenticating, Connected, Stopping, Failed
};
enum class Error : uint8_t {
  None, Memory, Join, Certificate, Socket, Handshake, Authentication, Disconnected, Storage, Discovery
};

struct Snapshot {
  State state;
  Error error;
  uint64_t nonce;
  char address[16];
  uint16_t port;
  char fingerprint[65];  // SHA-256 of DER certificate, lowercase hex.
  char token[65];        // Pairing secret: send over trusted USB only; never log.
  char deviceId[33];     // Stable, nonsecret random ID used for discovery.
  int32_t rssi;
  uint32_t receivedBytes;
  uint32_t sentBytes;
  uint32_t freeHeap;
  uint32_t minimumFreeHeap;
  uint32_t stackFreeBytes;
  int32_t tlsError;       // Last failing mbedTLS result; no credentials/content.
  // Session-cumulative performance counters; uint32 values may wrap. TLS times
  // include scheduler preemption. Socket counters include the TLS handshake.
  uint32_t tlsReadMicros;
  uint32_t tlsReadCalls;
  uint32_t socketRxBytes;
  uint32_t socketWouldBlock;
  uint32_t rxQueueFullTicks;
  uint32_t rxQueueHighWaterBytes;
};

// Main-loop-only API. All starts require explicit local display-mode consent.
// begin() accepts an explicitly entered local/USB network and saves it with a
// stable certificate/token. Network updates preserve the pairing identity.
// beginSaved() uses the shared saved network, preserving any paired identity;
// it creates an identity only if Display has never been paired. Neither starts
// automatically at boot.
bool begin(const char* ssid, const char* password, uint64_t nonce);
bool beginSaved(uint64_t nonce);
// These storage operations require busy()==false. Corrupt records fail closed;
// forgetSaved() removes only the shared network and preserves the TLS identity.
// A corrupt paired identity still fails closed and requires separate repair.
bool hasSaved();
// Main-loop-only, idle radio: copy the saved network name, never credentials.
// Always NUL-terminates nonempty output capacity; returns false on no saved network.
bool savedSsid(char* output, size_t capacity);
bool forgetSaved();
void stop();  // Asynchronous: sockets/radio/secret cleanup occurs in the worker.
bool busy(); // True until worker cleanup completes; begin() cannot overlap it.
void reap(); // Main loop only: free session queues after busy() becomes false.
size_t retainedBytes(); // Main-loop diagnostic; never includes secrets.
Snapshot snapshot();

// Nonblocking, bounded queues. read() returns authenticated COBS bytes only.
// write() is all-or-nothing and returns false if disconnected or the TX queue is
// full: the caller must end the display session rather than silently drop ACKs.
size_t read(uint8_t* data, size_t capacity);
bool write(const uint8_t* data, size_t length);

// TLS 1.2 ECDHE-ECDSA/AES-128-GCM. Mac pins snapshot.fingerprint before sending
// "MOSS AUTH <token>\n". Server replies "MOSS AUTH OK\n" before any MOSD traffic.
// One authenticated client/session; a failed authentication ends that session.

}  // namespace wifi_display
}  // namespace sloth
#endif
