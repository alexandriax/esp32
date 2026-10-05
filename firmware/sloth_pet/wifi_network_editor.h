#pragma once
#include <stddef.h>
#include "keyboard.h"
#include <stdint.h>
namespace sloth {
namespace graphics { struct Canvas; }
enum class WifiEditorPage : uint8_t { Network, Ssid, Password };
enum class WifiEditorEvent : uint8_t { None, Changed, Connect, Back };
// Shared credential editor. No radio, storage, logging, or allocation. Credentials
// survive Connect only until the caller copies them; show/close wipe all drafts.
class WifiNetworkEditor {
 public:
  enum { kSsid=0, kPassword=1, kConnect=2, kNetworkBack=3,
         kKeyCount=95, kDelete=95, kKeyboardBack=96, kDone=97, kClear=98,
         kNavBack=100, kNavNext=101, kNavSelect=102 };
  void show(const char* initialSsid="", bool passwordRequired=false);
  void close();
  bool isOpen() const { return open_; }
  WifiEditorPage page() const { return page_; }
  const char* ssid() const { return ssid_; }
  const char* password() const { return password_; }
  const char* editBuffer() const { return edit_; }
  bool passwordRequired() const { return passwordRequired_; }
  int focus() const { return page_==WifiEditorPage::Network?focus_:keyboard_.focus(); }
  int scrollOffset() const { return page_==WifiEditorPage::Network?0:keyboard_.scroll(); }
  int maxScroll() const;
  int targetCount() const;
  bool targetEnabled(int target) const;
  bool validNetwork() const;
  static char keyCharacter(int target);
  int hitTest(int x,int y) const;
  void next();
  WifiEditorEvent activate(int target=-1);
  WifiEditorEvent back();
  WifiEditorEvent touch(bool down,int x,int y);
  void cancelTouch();
  KeyboardSpec keyboardSpec() const;
  const Keyboard& keyboard() const { return keyboard_; }
 private:
  Keyboard keyboard_;
  WifiEditorEvent keyboardEvent(KeyboardEvent);
  bool open_=false,passwordRequired_=false,touching_=false,dragged_=false;
  WifiEditorPage page_=WifiEditorPage::Network;
  int focus_=0,startX_=0,startY_=0,touchTarget_=-1;
  char ssid_[33]={},password_[64]={},edit_[64]={};
  void eraseCredentials();
};
void drawWifiNetworkEditor(uint16_t* pixels,const WifiNetworkEditor& ui);
// Case-sensitive SSID labels use the same glyphs as credential entry.
void drawWifiLiteral(graphics::Canvas& canvas,int x,int y,const char* text,uint16_t color,size_t limit);
} // namespace sloth
