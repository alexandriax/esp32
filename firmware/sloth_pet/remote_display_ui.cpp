#include "remote_display_ui.h"
#include "pet_canvas.h"
#include "navigation_bar.h"
#include "ui_icons.h"
#include <string.h>
namespace sloth {
namespace {
bool inside(int x,int y,int l,int t,int w,int h) { return x>=l && x<l+w && y>=t && y<t+h; }
void button(graphics::Canvas& c,int y,const char* text,bool focus,bool enabled,UiIcon icon) {
  using namespace graphics;
  if(focus && enabled)c.roundRect(11,y-1,218,30,4,gold);
  c.roundRect(12,y,216,28,3,rgb(30,57,48));
  const int start=12+(216-(static_cast<int>(strlen(text))*6-1)-20)/2;
  drawUiIcon(c,start,y+6,icon,enabled?mint:muted);
  c.text(start+20,y+10,text,enabled?cream:muted);
}
}
void RemoteUi::show(RemoteStatus status,bool usbAvailable) {
  editor_.close();open_=true;status_=status;usb_=usbAvailable;focus_=0;cancelTouch();
}
void RemoteUi::editNetwork(const char* initialSsid) { cancelTouch();editor_.show(initialSsid);open_=true; }
void RemoteUi::close() { editor_.close();open_=false;cancelTouch(); }
RemotePage RemoteUi::page() const {
  if(!editor_.isOpen())return RemotePage::Status;
  return editor_.page()==WifiEditorPage::Ssid?RemotePage::Ssid:
         editor_.page()==WifiEditorPage::Password?RemotePage::Password:RemotePage::Network;
}
bool RemoteUi::targetEnabled(int target) const {
  if(!open_)return false;
  if(editor_.isOpen())return editor_.targetEnabled(target);
  if(target==kNavBack || target==kNavNext)return true;
  if(target==kNavSelect)return targetEnabled(focus_);
  return target>=0 && target<4 && (target!=kUseUSB || usb_);
}
void RemoteUi::cancelTouch() { touching_=dragged_=false;touchTarget_=-1;editor_.cancelTouch(); }
void RemoteUi::next() {
  if(!open_)return;
  if(editor_.isOpen()) { editor_.next();return; }
  cancelTouch();do {focus_=(focus_+1)%4;}while(!targetEnabled(focus_));
}
RemoteEvent RemoteUi::translate(WifiEditorEvent event) {
  if(event==WifiEditorEvent::Back) { show(RemoteStatus::NoHost,usb_);return RemoteEvent::Changed; }
  return event==WifiEditorEvent::Connect?RemoteEvent::ConnectNetwork:
         event==WifiEditorEvent::Changed?RemoteEvent::Changed:RemoteEvent::None;
}
RemoteEvent RemoteUi::activate(int target) {
  if(!open_)return RemoteEvent::None;
  if(editor_.isOpen())return translate(editor_.activate(target));
  if(target==kNavBack)return back();
  if(target==kNavNext){next();return RemoteEvent::Changed;}
  if(target<0 || target==kNavSelect)target=focus_;
  if(!targetEnabled(target))return RemoteEvent::None;
  cancelTouch();focus_=target;
  if(target==kRetry)return RemoteEvent::Retry;
  if(target==kEditNetwork){editNetwork();return RemoteEvent::EditNetwork;}
  return target==kUseUSB?RemoteEvent::UseUSB:RemoteEvent::Back;
}
RemoteEvent RemoteUi::back() {
  if(!open_)return RemoteEvent::None;
  cancelTouch();return editor_.isOpen()?translate(editor_.back()):RemoteEvent::Back;
}
int RemoteUi::hitTest(int x,int y) const {
  if(!open_ || x<0 || y<0 || x>=240 || y>=240)return -1;
  if(editor_.isOpen())return editor_.hitTest(x,y);
  if(inside(x,y,20,8,200,16))return x<86?kNavNext:x<154?kNavBack:kNavSelect;
  for(int i=0;i<4;++i)if(inside(x,y,12,101+i*33,216,28))return targetEnabled(i)?i:-1;
  return -1;
}
RemoteEvent RemoteUi::touch(bool down,int x,int y) {
  if(!open_)return RemoteEvent::None;
  if(editor_.isOpen())return translate(editor_.touch(down,x,y));
  if(down && !touching_) { touching_=true;startX_=x;startY_=y;touchTarget_=hitTest(x,y);return RemoteEvent::None; }
  if(down) { const int dx=x-startX_,dy=y-startY_;if(dx>7 || dx< -7 || dy>7 || dy< -7)dragged_=true;return RemoteEvent::None; }
  if(!touching_)return RemoteEvent::None;
  const int target=!dragged_ && hitTest(x,y)==touchTarget_?touchTarget_:-1;
  cancelTouch();return target<0?RemoteEvent::None:activate(target);
}
void drawRemoteDisplay(uint16_t* pixels,const RemoteUi& ui) {
  using namespace graphics;
  if(ui.editor().isOpen()) { drawWifiNetworkEditor(pixels,ui.editor());return; }
  if(!pixels)return;
  Canvas c{pixels};c.rect(0,0,240,240,ink);if(!ui.isOpen())return;
  drawNavigationBar(c);c.centered(30,"REMOTE DISPLAY",cream,2);
  const bool permission=ui.status()==RemoteStatus::PermissionNeeded;
  const char* title=permission?"ALLOW SCREEN RECORDING":ui.status()==RemoteStatus::Connecting?"FINDING YOUR MAC":ui.status()==RemoteStatus::NoHost?"MAC NOT FOUND":ui.status()==RemoteStatus::NoUSB?"CONNECT WITHOUT A CABLE":ui.status()==RemoteStatus::Ready?"READY TO CONNECT":"CONNECTION FAILED";
  c.centered(52,title,gold);
  c.centered(68,permission?"MAC SYSTEM SETTINGS / PRIVACY":"OPEN MOSS DISPLAY ON YOUR MAC",muted);
  c.centered(82,permission?"ENABLE MOSS DISPLAY, THEN RETRY":"NEW MAC? PAIR WITH USB FIRST",muted);
  const char* labels[]={ui.status()==RemoteStatus::Ready?"CONNECT AUTOMATICALLY":"TRY AGAIN","CHANGE WI-FI NETWORK","USE USB","BACK"};
  const UiIcon icons[]={UiIcon::Remote,UiIcon::Wifi,UiIcon::USB,UiIcon::Back};
  for(int i=0;i<4;++i)button(c,101+i*33,labels[i],ui.focus()==i,ui.targetEnabled(i),icons[i]);
}
} // namespace sloth
