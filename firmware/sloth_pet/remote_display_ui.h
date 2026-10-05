#pragma once
#include <stdint.h>
#include "wifi_network_editor.h"
namespace sloth {
enum class RemotePage : uint8_t { Status, Network, Ssid, Password };
enum class RemoteStatus : uint8_t { Connecting, NoHost, NoUSB, Failed, PermissionNeeded, Ready };
enum class RemoteEvent : uint8_t { None, Changed, Retry, EditNetwork, ConnectNetwork, UseUSB, Back };
// Remote status UI delegates credentials to the shared Wi-Fi editor. New firmware
// opens WifiNetworksUi on EditNetwork; the editor API remains for existing callers.
class RemoteUi {
 public:
  enum { kRetry=0, kEditNetwork=1, kUseUSB=2, kBack=3,
         kSsid=0, kPassword=1, kConnect=2, kNetworkBack=3,
         kKeyCount=95, kDelete=95, kKeyboardBack=96, kDone=97,kClear=98,
         kNavBack=100, kNavNext=101, kNavSelect=102 };
  void show(RemoteStatus status,bool usbAvailable);
  void editNetwork(const char* initialSsid="");
  void close();
  bool isOpen() const { return open_; }
  RemotePage page() const;
  RemoteStatus status() const { return status_; }
  bool usbAvailable() const { return usb_; }
  void setUsbAvailable(bool available) { usb_=available; }
  const char* ssid() const { return editor_.ssid(); }
  const char* password() const { return editor_.password(); }
  const char* editBuffer() const { return editor_.editBuffer(); }
  int focus() const { return editor_.isOpen()?editor_.focus():focus_; }
  int scrollOffset() const { return editor_.isOpen()?editor_.scrollOffset():0; }
  int maxScroll() const { return editor_.isOpen()?editor_.maxScroll():0; }
  int targetCount() const { return !open_?0:editor_.isOpen()?editor_.targetCount():4; }
  bool targetEnabled(int target) const;
  bool validNetwork() const { return editor_.validNetwork(); }
  static char keyCharacter(int target) { return WifiNetworkEditor::keyCharacter(target); }
  int hitTest(int x,int y) const;
  void next();
  RemoteEvent activate(int target=-1);
  RemoteEvent back();
  RemoteEvent touch(bool down,int x,int y);
  void cancelTouch();
  const WifiNetworkEditor& editor() const { return editor_; }
 private:
  bool open_=false,usb_=false,touching_=false,dragged_=false;
  RemoteStatus status_=RemoteStatus::Connecting;
  int focus_=0,startX_=0,startY_=0,touchTarget_=-1;
  WifiNetworkEditor editor_;
  RemoteEvent translate(WifiEditorEvent event);
};
void drawRemoteDisplay(uint16_t* pixels,const RemoteUi& ui);
} // namespace sloth
