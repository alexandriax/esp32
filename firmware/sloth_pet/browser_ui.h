#pragma once
#include <stddef.h>
#include "keyboard.h"
#include "browser_viewport.h"
#include <stdint.h>

namespace sloth {
enum class BrowserPage : uint8_t { Page, Url, ConfirmExit };
enum class BrowserEventType : uint8_t { None, Changed, Navigate, Reload, Back, Cancel, Wifi, Exit, Scroll, Link, Mode, RequestExit };
struct BrowserEvent {
  BrowserEventType type;
  int delta, x, y;
  BrowserEvent(BrowserEventType value=BrowserEventType::None,int amount=0,int px=0,int py=0)
      : type(value),delta(amount),x(px),y(py) {}
};

// Main-loop state only: no network, allocation, framebuffer, or history ownership.
// Touch input is logical 240x240. Scroll delta and Link coordinates are native
// pixels; links are relative to the page viewport in browser_viewport.h.
class BrowserUi {
 public:
  enum { kUrlCapacity=512, kStatusCapacity=40,
         kUrl=0,kBack=1,kReload=2,kWifi=3,kExit=4,kScrollUp=5,kScrollDown=6,kMode=7,
         kKeyCount=95,kDelete=95,kClear=96,kKeyboardBack=97,kDone=98,
         kNavBack=100,kNavNext=101,kNavSelect=102 };
  void show(const char* url=nullptr);
  void close();
  void requestExit();
  bool isOpen() const { return open_; }
  BrowserPage page() const { return page_; }
  const char* url() const { return url_; }
  const char* draft() const { return draft_; }
  const char* status() const { return status_; }
  // Reject an overlong URL instead of silently navigating to its prefix.
  bool setUrl(const char* url);
  void setStatus(const char* status);
  void setLoading(bool loading);
  void setCanGoBack(bool available) { canGoBack_=available; }
  void setHasPage(bool available);
  bool textMode() const {return textMode_;}
  bool hasPage() const { return hasPage_; }
  bool loading() const { return loading_; }
  bool canGoBack() const { return canGoBack_; }
  int focus() const { return page_==BrowserPage::Url?keyboard_.focus():focus_; }
  int scrollOffset() const { return page_==BrowserPage::Url?keyboard_.scroll():scroll_; }
  int maxScroll() const;
  static char keyCharacter(int target);
  bool targetEnabled(int target) const;
  int hitTest(int x,int y) const;
  void next();
  BrowserEvent activate(int target=-1);
  BrowserEvent back();
  BrowserEvent touch(bool down,int x,int y);
  void cancelTouch();
  KeyboardSpec keyboardSpec() const;
  const Keyboard& keyboard() const { return keyboard_; }
 private:
  Keyboard keyboard_;
  BrowserEvent keyboardEvent(KeyboardEvent);
  bool textMode_=true;
  bool open_=false,loading_=false,canGoBack_=false,hasPage_=false;
  bool touching_=false,dragged_=false,scrollGesture_=false;
  BrowserPage page_=BrowserPage::Page,returnPage_=BrowserPage::Page;
  int focus_=kUrl,scroll_=0,startX_=0,startY_=0,lastY_=0,startScroll_=0,touchTarget_=-1;
  char url_[kUrlCapacity]={},draft_[kUrlCapacity]={},status_[kStatusCapacity]={};
};

// Each call writes exactly one 240-pixel RGB565 row. No full frame is allocated.
// With a rendered page, draw only chrome rows so the engine's content survives.
inline bool browserChromeRow(unsigned y) { return y<BROWSER_PAGE_TOP || (y>=BROWSER_PAGE_BOTTOM && y<240); }
void drawBrowserRow(uint16_t* row,const BrowserUi& ui,unsigned y);
} // namespace sloth
