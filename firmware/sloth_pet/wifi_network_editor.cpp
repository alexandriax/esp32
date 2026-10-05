#include "wifi_network_editor.h"
#include "pet_canvas.h"
#include "navigation_bar.h"
#include "ui_icons.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace sloth {
namespace {
bool isKeyboard(WifiEditorPage p) { return p==WifiEditorPage::Ssid || p==WifiEditorPage::Password; }
void wipe(void* p,size_t n) { volatile uint8_t* v=static_cast<volatile uint8_t*>(p); while(n--) *v++=0; }
void copy(char* out,size_t capacity,const char* text) {
  size_t n=0;
  if(text) while(n+1<capacity && text[n]) { out[n]=text[n]; ++n; }
  out[n]=0;
}
bool inside(int x,int y,int l,int t,int w,int h) { return x>=l && x<l+w && y>=t && y<t+h; }
bool ascii(const char* text) {
  for(const unsigned char* c=reinterpret_cast<const unsigned char*>(text);*c;++c)
    if(*c<32 || *c>126) return false;
  return true;
}
using namespace graphics;
// Credentials are case-sensitive. This font supplements Canvas's uppercase-only
// labels with distinct lowercase and every printable punctuation character.
void glyph(Canvas& c,int x,int y,char ch,uint16_t color) { const char text[]={ch,0};drawKeyboardText(c.pixels,x,y,text,color,1); }
void literal(Canvas& c,int x,int y,const char* text,uint16_t color,size_t limit) {
  for(size_t i=0;i<limit && text[i];++i) glyph(c,x+6*static_cast<int>(i),y,static_cast<unsigned char>(text[i])>=32 && static_cast<unsigned char>(text[i])<=126?text[i]:'?',color);
}
void button(Canvas& c,int x,int y,int width,const char* text,bool focus,bool enabled=true,UiIcon icon=UiIcon::None) {
  if(focus && enabled) c.roundRect(x-1,y-1,width+2,30,4,gold);
  c.roundRect(x,y,width,28,3,rgb(30,57,48));
  const bool hasIcon=icon!=UiIcon::None;
  const int start=x+(width-(static_cast<int>(strlen(text))*6-1)-(hasIcon?20:0))/2;
  if(hasIcon)drawUiIcon(c,start,y+6,icon,enabled?mint:muted);
  c.text(start+(hasIcon?20:0),y+10,text,enabled?cream:muted);
}
void tail(Canvas& c,int x,int y,const char* text,bool mask,size_t limit) {
  const size_t length=strlen(text),offset=length>limit?length-limit:0;
  if(mask) for(size_t i=offset;i<length;++i) glyph(c,x+6*static_cast<int>(i-offset),y,'*',cream);
  else literal(c,x,y,text+offset,cream,limit);
}
} // namespace
KeyboardSpec WifiNetworkEditor::keyboardSpec() const {
  KeyboardSpec s;s.text=edit_;s.keys=Keyboard::asciiKeys();s.title=page_==WifiEditorPage::Ssid?"NETWORK NAME":"WI-FI PASSWORD";
  s.capacity=page_==WifiEditorPage::Ssid?33:64;s.masked=page_==WifiEditorPage::Password;
  s.doneEnabled=page_!=WifiEditorPage::Ssid || edit_[0];s.actions[1]=kClear;s.actions[2]=kKeyboardBack;s.actions[3]=kDone;return s;
}
WifiEditorEvent WifiNetworkEditor::keyboardEvent(KeyboardEvent event){
  if(event==KeyboardEvent::Back || event==KeyboardEvent::Cancel)return back();
  if(event==KeyboardEvent::Done){copy(page_==WifiEditorPage::Ssid?ssid_:password_,page_==WifiEditorPage::Ssid?sizeof(ssid_):sizeof(password_),edit_);return back();}
  return event==KeyboardEvent::Changed?WifiEditorEvent::Changed:WifiEditorEvent::None;
}
void WifiNetworkEditor::eraseCredentials() { wipe(ssid_,sizeof(ssid_));wipe(password_,sizeof(password_));wipe(edit_,sizeof(edit_)); }
void WifiNetworkEditor::close() { eraseCredentials();open_=false;cancelTouch(); }
void WifiNetworkEditor::show(const char* initialSsid,bool passwordRequired) {
  eraseCredentials();copy(ssid_,sizeof(ssid_),initialSsid);open_=true;page_=WifiEditorPage::Network;
  passwordRequired_=passwordRequired;focus_=0;cancelTouch();
}
bool WifiNetworkEditor::validNetwork() const { const size_t n=strlen(password_);return ssid_[0] && ascii(password_) && ((!n && !passwordRequired_) || n>=8); }
int WifiNetworkEditor::maxScroll() const { return isKeyboard(page_)?Keyboard::maxScroll(keyboardSpec()):0; }
int WifiNetworkEditor::targetCount() const { return !open_?0:isKeyboard(page_)?99:4; }
char WifiNetworkEditor::keyCharacter(int target) { return target>=0 && target<kKeyCount?Keyboard::asciiKeys()[target]:0; }
bool WifiNetworkEditor::targetEnabled(int target) const {
  if(open_ && (target==kNavBack || target==kNavNext))return true;
  if(open_ && target==kNavSelect)return targetEnabled(focus());
  if(target<0 || target>=targetCount()) return false;
  if(page_==WifiEditorPage::Network) return target!=kConnect || validNetwork();
  return Keyboard::enabled(keyboardSpec(),target);
}
void WifiNetworkEditor::cancelTouch() { keyboard_.cancelTouch(); touching_=dragged_=false;touchTarget_=-1; }
void WifiNetworkEditor::next() {
  if(!open_) return;
  if(isKeyboard(page_)){keyboard_.next(keyboardSpec());return;}
  cancelTouch();for(int i=0;i<targetCount();++i) { focus_=(focus_+1)%targetCount();if(targetEnabled(focus_))break; }
}
WifiEditorEvent WifiNetworkEditor::activate(int target) {
  if(!open_)return WifiEditorEvent::None;
  if(isKeyboard(page_))return keyboardEvent(keyboard_.activate(keyboardSpec(),edit_,target));
  if(target==kNavBack)return back();
  if(target==kNavNext){next();return WifiEditorEvent::Changed;}
  if(target==kNavSelect)target=focus_;
  if(target<0) target=focus_;
  if(!targetEnabled(target)) return WifiEditorEvent::None;
  cancelTouch();focus_=target;
  if(page_==WifiEditorPage::Network) {
    if(target==kConnect)return WifiEditorEvent::Connect;
    if(target==kNetworkBack)return back();
    page_=target==kSsid?WifiEditorPage::Ssid:WifiEditorPage::Password;
    copy(edit_,sizeof(edit_),target==kSsid?ssid_:password_);focus_=0;keyboard_.reset();
    return WifiEditorEvent::Changed;
  }
  return WifiEditorEvent::None;
}
WifiEditorEvent WifiNetworkEditor::back() {
  cancelTouch();if(!open_)return WifiEditorEvent::None;
  if(page_==WifiEditorPage::Network)return WifiEditorEvent::Back;
  focus_=page_==WifiEditorPage::Ssid?kSsid:kPassword;page_=WifiEditorPage::Network;wipe(edit_,sizeof(edit_));return WifiEditorEvent::Changed;
}
int WifiNetworkEditor::hitTest(int x,int y) const {
  if(!open_ || x<0 || y<0 || x>=240 || y>=240)return -1;
  if(inside(x,y,20,8,200,16)) {
    const int nav=x<86?kNavNext:x<154?kNavBack:kNavSelect;
    return targetEnabled(nav)?nav:-1;
  }
  if(isKeyboard(page_))return keyboard_.hitTest(keyboardSpec(),x,y);
  {
    const int top=64;
    if(page_==WifiEditorPage::Network) {
      for(int i=0;i<2;++i)if(inside(x,y,12,top+i*53,216,40))return i;
      for(int i=0;i<2;++i)if(inside(x,y,12+i*112,202,104,28))return targetEnabled(i+2)?i+2:-1;
    }
  }
  return -1;
}
WifiEditorEvent WifiNetworkEditor::touch(bool down,int x,int y) {
  if(!open_)return WifiEditorEvent::None;
  if(isKeyboard(page_))return keyboardEvent(keyboard_.touch(keyboardSpec(),edit_,down,x,y));
  if(down && !touching_) {
    touching_=true;startX_=x;startY_=y;touchTarget_=hitTest(x,y);
    return WifiEditorEvent::None;
  }
  if(down) {
    const int dx=x-startX_,dy=y-startY_;
    if(dx>7 || dx< -7 || dy>7 || dy< -7)dragged_=true;
    return WifiEditorEvent::None;
  }
  if(!touching_)return WifiEditorEvent::None;
  const int target=(!dragged_ && hitTest(x,y)==touchTarget_)?touchTarget_:-1;
  cancelTouch();return target<0?WifiEditorEvent::None:activate(target);
}
void drawWifiNetworkEditor(uint16_t* pixels,const WifiNetworkEditor& ui) {
  if(!pixels)return;
  Canvas c{pixels};c.rect(0,0,240,240,ink);if(!ui.isOpen())return;
  drawNavigationBar(c);
  if(isKeyboard(ui.page())){drawKeyboard(pixels,ui.keyboardSpec(),ui.keyboard());return;}
  c.centered(30,"WI-FI NETWORK",cream,2);
  if(ui.page()==WifiEditorPage::Network) {
    c.centered(49,"2.4 GHZ WI-FI",muted);
    for(int i=0;i<2;++i) {
      const int y=64+i*53;
      if(ui.focus()==i)c.roundRect(11,y-1,218,42,4,gold);
      c.roundRect(12,y,216,40,4,rgb(30,57,48));
      drawUiIcon(c,20,y+11,i?UiIcon::Lock:UiIcon::Wifi,mint);
      c.text(44,y+6,i?"PASSWORD":"NETWORK NAME",muted);
      const char* value=i?ui.password():ui.ssid();
      if(*value)tail(c,44,y+23,value,i!=0,29);else c.text(44,y+23,i?(ui.passwordRequired()?"ENTER NETWORK PASSWORD":"EMPTY FOR OPEN NETWORK"):"TAP TO ENTER",cream);
    }
    c.centered(173,ui.passwordRequired()?"PASSWORD: 8-63 CHARACTERS":"PASSWORD: EMPTY OR 8-63 CHARS",muted);
    c.centered(186,ui.passwordRequired()?"PASSWORD REQUIRED":"EMPTY PASSWORD: OPEN NETWORK",gold);
    button(c,12,202,104,"CONNECT",ui.focus()==2,ui.validNetwork(),UiIcon::Wifi);button(c,124,202,104,"BACK",ui.focus()==3,true,UiIcon::Back);
  }
}
void drawWifiLiteral(graphics::Canvas& canvas,int x,int y,const char* text,uint16_t color,size_t limit) {
  if(text)literal(canvas,x,y,text,color,limit);
}
} // namespace sloth
