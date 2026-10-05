#include "../firmware/sloth_pet/remote_connection.h"
#include <cassert>
#include <cstdio>
int main() {
  using namespace sloth;
  assert(chooseRemoteTransport(true,true) == RemoteTransport::Wifi);
  assert(chooseRemoteTransport(true,false) == RemoteTransport::Wifi);
  assert(chooseRemoteTransport(false,true) == RemoteTransport::Usb);
  assert(chooseRemoteTransport(false,false) == RemoteTransport::NetworkSetup);
  assert(!remoteAttemptTimedOut(true,100,0,false,30099));
  assert(remoteAttemptTimedOut(true,100,0,false,30100));
  assert(!remoteAttemptTimedOut(true,100,5000,true,16999));
  assert(remoteAttemptTimedOut(true,100,5000,true,17000));
  assert(!remoteAttemptTimedOut(false,100,0,false,8099));
  assert(remoteAttemptTimedOut(false,100,0,false,8100));
  assert(remoteAttemptTimedOut(false,UINT32_MAX-3999u,0,false,4000));
  assert(remoteAttemptTimedOut(true,UINT32_MAX-14999u,UINT32_MAX-5999u,true,6000));
  assert(shouldFallbackToUsb(true,false,false,true));
  assert(!shouldFallbackToUsb(true,true,false,true)); // Never migrate a live display silently.
  assert(!shouldFallbackToUsb(true,false,true,true)); // No retry loop.
  assert(!shouldFallbackToUsb(true,false,false,false)); // Cable required.
  assert(!shouldFallbackToUsb(false,false,false,true));
  std::puts("Remote connection: Wi-Fi preference, USB fallback, network setup, bounded deadlines and rollover passed");
}
