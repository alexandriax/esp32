#pragma once
#include "wifi_networks.h"
#include "wifi_network_editor.h"
namespace sloth {
enum class WifiNetworksPage : uint8_t { Networks, Credentials, ConfirmForget };
enum class WifiNetworksEvent : uint8_t { None, Changed, Scan, ConnectSaved, ConnectNetwork, Forget, Back };
// Pure main-loop UI. Backend operations and radio ownership belong to the caller.
// Copy ssid/password synchronously on ConnectNetwork, then credentialsSubmitted()
// wipes them. Backend snapshots contain names/status only, never passwords.
class WifiNetworksUi {
 public:
  enum { kScan=20,kManual=21,kForget=22,kBack=23,
         kConfirmForget=0,kCancelForget=1,
         kNavBack=100,kNavNext=101,kNavSelect=102 };
  void show(const wifi_networks::Snapshot& state);
  void close();
  bool update(const wifi_networks::Snapshot& state);
  void setWaiting(bool waiting);
  void setNotice(const char* message);
  void credentialsSubmitted();
  bool isOpen() const { return open_; }
  bool waiting() const { return waiting_; }
  const char* notice() const { return notice_; }
  WifiNetworksPage page() const { return page_; }
  const wifi_networks::Snapshot& snapshot() const { return snapshot_; }
  const WifiNetworkEditor& editor() const { return editor_; }
  const char* ssid() const { return editor_.ssid(); }
  const char* password() const { return editor_.password(); }
  int focus() const { return page_==WifiNetworksPage::Credentials?editor_.focus():focus_; }
  int rowCount() const;
  const char* rowSsid(int row) const;
  const wifi_networks::AccessPoint* accessPoint(int row) const;
  bool savedRow(int row) const { return snapshot_.hasSaved && row==0; }
  int scrollOffset() const { return page_==WifiNetworksPage::Credentials?editor_.scrollOffset():scroll_; }
  int maxScroll() const;
  bool targetEnabled(int target) const;
  int hitTest(int x,int y) const;
  void next();
  WifiNetworksEvent activate(int target=-1);
  WifiNetworksEvent back();
  WifiNetworksEvent touch(bool down,int x,int y);
  void cancelTouch();
 private:
  bool open_=false,waiting_=false,touching_=false,dragged_=false,scrollGesture_=false;
  WifiNetworksPage page_=WifiNetworksPage::Networks;
  wifi_networks::Snapshot snapshot_={};
  WifiNetworkEditor editor_;
  char notice_[36]={};
  int focus_=kScan,scroll_=0,startX_=0,startY_=0,startScroll_=0,touchTarget_=-1;
  bool busy() const;
  void revealFocus();
  WifiNetworksEvent translate(WifiEditorEvent event);
};
void drawWifiNetworks(uint16_t* pixels,const WifiNetworksUi& ui);
} // namespace sloth
