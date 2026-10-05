#pragma once
#include <stdint.h>

namespace sloth {
enum class RemoteTransport : uint8_t { NetworkSetup, Wifi, Usb };
inline RemoteTransport chooseRemoteTransport(bool savedWifi, bool usbConnected) {
  return savedWifi ? RemoteTransport::Wifi : usbConnected ? RemoteTransport::Usb : RemoteTransport::NetworkSetup;
}
inline bool remoteAttemptTimedOut(bool wifi, uint32_t started, uint32_t listening,
                                 bool listeningStarted, uint32_t now) {
  return wifi ? now - started >= 30000u || (listeningStarted && now - listening >= 12000u)
              : now - started >= 8000u;
}
inline bool shouldFallbackToUsb(bool wifiAttempt, bool receivedPixels, bool triedUsb, bool usbConnected) {
  return wifiAttempt && !receivedPixels && !triedUsb && usbConnected;
}
} // namespace sloth
