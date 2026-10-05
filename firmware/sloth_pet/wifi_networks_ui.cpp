#include "wifi_networks_ui.h"
#include "pet_canvas.h"
#include "navigation_bar.h"
#include "ui_icons.h"
#include <stdio.h>
#include <string.h>
namespace sloth {
namespace {
const int listTop=78,listBottom=180,rowPitch=34,rowHeight=30;
bool inside(int x,int y,int l,int t,int w,int h) { return x>=l && x<l+w && y>=t && y<t+h; }
void copy(char* out,size_t capacity,const char* text) {
  size_t n=0;if(text)while(n+1<capacity && text[n]){out[n]=text[n];++n;}out[n]=0;
}
bool sameRows(const wifi_networks::Snapshot& a,const wifi_networks::Snapshot& b) {
  if(a.count!=b.count || a.hasSaved!=b.hasSaved || strcmp(a.savedSsid,b.savedSsid))return false;
  for(unsigned i=0;i<a.count;++i)
    if(strcmp(a.networks[i].ssid,b.networks[i].ssid) || a.networks[i].supported!=b.networks[i].supported || a.networks[i].secured!=b.networks[i].secured)return false;
  return true;
}
using namespace graphics;
void button(Canvas& c,int x,int y,int width,const char* text,bool focus,bool enabled,UiIcon icon) {
  if(focus && enabled)c.roundRect(x-1,y-1,width+2,25,4,gold);
  c.roundRect(x,y,width,23,3,rgb(30,57,48));
  drawUiIcon(c,x+5,y+4,icon,enabled?mint:muted);
  c.text(x+26,y+8,text,enabled?cream:muted);
}
const char* errorText(wifi_networks::Error error) {
  using wifi_networks::Error;
  switch(error) {
    case Error::Busy:return "WI-FI IS BUSY";
    case Error::InvalidCredentials:return "CHECK NETWORK AND PASSWORD";
    case Error::Radio:return "WI-FI COULD NOT START";
    case Error::Scan:return "SCAN FAILED - TRY AGAIN";
    case Error::Join:return "COULD NOT JOIN NETWORK";
    case Error::Timeout:return "CONNECTION TIMED OUT";
    case Error::Disconnected:return "WI-FI DISCONNECTED";
    case Error::Storage:return "NETWORK COULD NOT BE SAVED";
    default:return "CONNECTION FAILED";
  }
}
}
void WifiNetworksUi::show(const wifi_networks::Snapshot& state) {
  close();open_=true;page_=WifiNetworksPage::Networks;focus_=kScan;update(state);
  if(rowCount() && targetEnabled(0))focus_=0;
}
void WifiNetworksUi::close() {
  editor_.close();open_=false;waiting_=false;page_=WifiNetworksPage::Networks;
  snapshot_={};notice_[0]=0;focus_=kScan;scroll_=0;cancelTouch();
}
bool WifiNetworksUi::update(const wifi_networks::Snapshot& state) {
  wifi_networks::Snapshot next=state;
  if(next.count>wifi_networks::kMaxNetworks)next.count=wifi_networks::kMaxNetworks;
  next.currentSsid[32]=next.savedSsid[32]=0;next.address[15]=0;
  for(unsigned i=0;i<next.count;++i)next.networks[i].ssid[32]=0;
  if(!next.savedSsid[0])next.hasSaved=false;
  const bool rowsChanged=!sameRows(snapshot_,next);
  bool changed=rowsChanged || snapshot_.state!=next.state || snapshot_.error!=next.error ||
      snapshot_.generation!=next.generation || snapshot_.rssi!=next.rssi ||
      strcmp(snapshot_.currentSsid,next.currentSsid) || strcmp(snapshot_.address,next.address);
  for(unsigned i=0;i<next.count && !changed;++i)changed=snapshot_.networks[i].rssi!=next.networks[i].rssi;
  snapshot_=next;
  if(rowsChanged && page_!=WifiNetworksPage::Credentials) {
    cancelTouch();scroll_=0;focus_=rowCount()?0:kScan;
    if(page_==WifiNetworksPage::ConfirmForget && !snapshot_.hasSaved && snapshot_.error!=wifi_networks::Error::Storage)page_=WifiNetworksPage::Networks;
  }
  if(page_==WifiNetworksPage::Networks && !targetEnabled(focus_))this->next();
  return changed;
}
void WifiNetworksUi::setWaiting(bool waiting) {
  if(waiting_!=waiting){
    if(waiting && page_==WifiNetworksPage::Credentials)credentialsSubmitted();
    waiting_=waiting;cancelTouch();if(!targetEnabled(focus_))next();
  }
}
void WifiNetworksUi::setNotice(const char* message) { copy(notice_,sizeof(notice_),message); }
void WifiNetworksUi::credentialsSubmitted() {
  editor_.close();page_=WifiNetworksPage::Networks;focus_=kScan;scroll_=0;cancelTouch();
}
bool WifiNetworksUi::busy() const {
  return waiting_ || snapshot_.state==wifi_networks::State::Scanning || snapshot_.state==wifi_networks::State::Joining;
}
int WifiNetworksUi::rowCount() const {
  int count=snapshot_.hasSaved?1:0;
  for(unsigned i=0;i<snapshot_.count;++i)
    if(snapshot_.networks[i].ssid[0] && (!snapshot_.hasSaved || strcmp(snapshot_.networks[i].ssid,snapshot_.savedSsid)))++count;
  return count;
}
const wifi_networks::AccessPoint* WifiNetworksUi::accessPoint(int row) const {
  if(row<0 || savedRow(row))return nullptr;
  int at=snapshot_.hasSaved?1:0;
  for(unsigned i=0;i<snapshot_.count;++i) {
    const auto& ap=snapshot_.networks[i];
    if(!ap.ssid[0] || (snapshot_.hasSaved && !strcmp(ap.ssid,snapshot_.savedSsid)))continue;
    if(at++==row)return &ap;
  }
  return nullptr;
}
const char* WifiNetworksUi::rowSsid(int row) const {
  if(savedRow(row))return snapshot_.savedSsid;
  const auto* ap=accessPoint(row);return ap?ap->ssid:"";
}
int WifiNetworksUi::maxScroll() const {
  if(page_==WifiNetworksPage::Credentials)return editor_.maxScroll();
  const int maximum=rowCount()*rowPitch-(listBottom-listTop);return maximum>0?maximum:0;
}
bool WifiNetworksUi::targetEnabled(int target) const {
  if(!open_)return false;
  if(page_==WifiNetworksPage::Credentials)return editor_.targetEnabled(target);
  if(target==kNavBack || target==kNavNext)return true;
  if(target==kNavSelect)return targetEnabled(focus_);
  if(page_==WifiNetworksPage::ConfirmForget)return target==kCancelForget || (target==kConfirmForget && (snapshot_.hasSaved || snapshot_.error==wifi_networks::Error::Storage) && !busy());
  if(target==kBack)return true;
  if(busy())return false;
  if(target==kScan || target==kManual)return true;
  if(target==kForget)return snapshot_.hasSaved || snapshot_.error==wifi_networks::Error::Storage;
  if(target<0 || target>=rowCount())return false;
  const auto* ap=accessPoint(target);return savedRow(target) || (ap && ap->supported);
}
void WifiNetworksUi::cancelTouch() { touching_=dragged_=scrollGesture_=false;touchTarget_=-1;editor_.cancelTouch(); }
void WifiNetworksUi::revealFocus() {
  if(page_!=WifiNetworksPage::Networks || focus_<0 || focus_>=rowCount())return;
  const int top=focus_*rowPitch;
  if(top<scroll_)scroll_=top;
  if(top+rowHeight>scroll_+listBottom-listTop)scroll_=top+rowHeight-(listBottom-listTop);
}
void WifiNetworksUi::next() {
  if(!open_)return;
  if(page_==WifiNetworksPage::Credentials){editor_.next();return;}
  cancelTouch();
  if(page_==WifiNetworksPage::ConfirmForget){focus_=focus_==kConfirmForget?kCancelForget:kConfirmForget;if(!targetEnabled(focus_))focus_=kCancelForget;return;}
  for(int n=0;n<rowCount()+4;++n) {
    if(focus_>=kScan && focus_<kBack)++focus_;
    else if(focus_==kBack)focus_=rowCount()?0:kScan;
    else focus_=focus_+1<rowCount()?focus_+1:kScan;
    if(targetEnabled(focus_))break;
  }
  revealFocus();
}
WifiNetworksEvent WifiNetworksUi::translate(WifiEditorEvent event) {
  if(event==WifiEditorEvent::Back){credentialsSubmitted();return WifiNetworksEvent::Changed;}
  return event==WifiEditorEvent::Connect?WifiNetworksEvent::ConnectNetwork:
      event==WifiEditorEvent::Changed?WifiNetworksEvent::Changed:WifiNetworksEvent::None;
}
WifiNetworksEvent WifiNetworksUi::activate(int target) {
  if(!open_)return WifiNetworksEvent::None;
  if(page_==WifiNetworksPage::Credentials)return translate(editor_.activate(target));
  if(target==kNavBack)return back();
  if(target==kNavNext){next();return WifiNetworksEvent::Changed;}
  if(target<0 || target==kNavSelect)target=focus_;
  if(!targetEnabled(target))return WifiNetworksEvent::None;
  cancelTouch();focus_=target;
  if(page_==WifiNetworksPage::ConfirmForget) {
    page_=WifiNetworksPage::Networks;focus_=kForget;
    return target==kConfirmForget?WifiNetworksEvent::Forget:WifiNetworksEvent::Changed;
  }
  if(target==kBack)return WifiNetworksEvent::Back;
  notice_[0]=0;
  if(target==kScan)return WifiNetworksEvent::Scan;
  if(target==kForget){page_=WifiNetworksPage::ConfirmForget;focus_=kCancelForget;return WifiNetworksEvent::Changed;}
  if(savedRow(target))return WifiNetworksEvent::ConnectSaved;
  const auto* ap=accessPoint(target);
  editor_.show(ap?ap->ssid:"",ap && ap->secured);
  page_=WifiNetworksPage::Credentials;return WifiNetworksEvent::Changed;
}
WifiNetworksEvent WifiNetworksUi::back() {
  if(!open_)return WifiNetworksEvent::None;
  cancelTouch();
  if(page_==WifiNetworksPage::Credentials)return translate(editor_.back());
  if(page_==WifiNetworksPage::ConfirmForget){page_=WifiNetworksPage::Networks;focus_=kForget;return WifiNetworksEvent::Changed;}
  return WifiNetworksEvent::Back;
}
int WifiNetworksUi::hitTest(int x,int y) const {
  if(!open_ || x<0 || y<0 || x>=240 || y>=240)return -1;
  if(page_==WifiNetworksPage::Credentials)return editor_.hitTest(x,y);
  if(inside(x,y,20,8,200,16)){const int nav=x<86?kNavNext:x<154?kNavBack:kNavSelect;return targetEnabled(nav)?nav:-1;}
  if(page_==WifiNetworksPage::ConfirmForget) {
    for(int i=0;i<2;++i)if(inside(x,y,12+i*112,198,104,23))return targetEnabled(i)?i:-1;
    return -1;
  }
  for(int i=0;i<rowCount();++i) {
    const int top=listTop+i*rowPitch-scroll_;
    if(top>=listTop && top+rowHeight<=listBottom && inside(x,y,12,top,216,rowHeight))return targetEnabled(i)?i:-1;
  }
  for(int i=0;i<4;++i)if(inside(x,y,12+(i%2)*112,184+(i/2)*28,104,23))return targetEnabled(kScan+i)?kScan+i:-1;
  return -1;
}
WifiNetworksEvent WifiNetworksUi::touch(bool down,int x,int y) {
  if(!open_)return WifiNetworksEvent::None;
  if(page_==WifiNetworksPage::Credentials)return translate(editor_.touch(down,x,y));
  if(down && !touching_) {
    touching_=true;startX_=x;startY_=y;startScroll_=scroll_;touchTarget_=hitTest(x,y);
    scrollGesture_=page_==WifiNetworksPage::Networks && y>=listTop && y<listBottom;
    return WifiNetworksEvent::None;
  }
  if(down) {
    const int dx=x-startX_,dy=y-startY_;if(dx>7 || dx< -7 || dy>7 || dy< -7)dragged_=true;
    if(dragged_ && scrollGesture_){const int value=startScroll_-dy;scroll_=value<0?0:value>maxScroll()?maxScroll():value;return WifiNetworksEvent::Changed;}
    return WifiNetworksEvent::None;
  }
  if(!touching_)return WifiNetworksEvent::None;
  const int target=!dragged_ && hitTest(x,y)==touchTarget_?touchTarget_:-1;
  cancelTouch();return target<0?WifiNetworksEvent::None:activate(target);
}
void drawWifiNetworks(uint16_t* pixels,const WifiNetworksUi& ui) {
  if(ui.page()==WifiNetworksPage::Credentials){drawWifiNetworkEditor(pixels,ui.editor());return;}
  if(!pixels)return;
  Canvas c{pixels};c.rect(0,0,240,240,ink);if(!ui.isOpen())return;
  drawNavigationBar(c);c.centered(30,"WI-FI NETWORKS",cream,2);
  if(ui.page()==WifiNetworksPage::ConfirmForget) {
    c.centered(69,"FORGET SAVED NETWORK?",gold);
    if(ui.snapshot().hasSaved)drawWifiLiteral(c,24,94,ui.snapshot().savedSsid,cream,32);
    else c.centered(94,"UNREADABLE SAVED NETWORK",cream);
    c.centered(121,"YOU CAN CONNECT AGAIN LATER",muted);
    c.centered(137,"MAC PAIRING WILL BE KEPT",muted);
    button(c,12,198,104,"FORGET",ui.focus()==0,ui.targetEnabled(0),UiIcon::Delete);
    button(c,124,198,104,"CANCEL",ui.focus()==1,true,UiIcon::Back);return;
  }
  using wifi_networks::State;
  const auto& state=ui.snapshot();
  const char* status=ui.waiting()?"FINISHING WI-FI SESSION":state.state==State::Scanning?"SCANNING 2.4 GHZ...":
      state.state==State::Joining?"CONNECTING...":state.state==State::Connected?"CONNECTED":
      state.state==State::Failed?errorText(state.error):"CHOOSE A 2.4 GHZ NETWORK";
  c.centered(51,status,state.state==State::Failed?gold:mint);
  if(*ui.notice())c.centered(64,ui.notice(),gold);
  else if(state.error==wifi_networks::Error::Storage)c.centered(64,"SAVED NETWORK NEEDS ATTENTION",gold);
  else if(state.state==State::Connected)c.centered(64,state.address,muted);
  else if(state.state==State::Joining)drawWifiLiteral(c,24,64,state.currentSsid,muted,32);
  else c.centered(64,ui.rowCount()?"SWIPE LIST TO SEE MORE":"SCAN OR ENTER NETWORK MANUALLY",muted);
  for(int i=0;i<ui.rowCount();++i) {
    const int y=listTop+i*rowPitch-ui.scrollOffset();
    if(y<listTop || y+rowHeight>listBottom)continue;
    const bool enabled=ui.targetEnabled(i);
    if(ui.focus()==i && enabled)c.roundRect(11,y-1,218,rowHeight+2,4,gold);
    c.roundRect(12,y,216,rowHeight,3,rgb(30,57,48));
    const auto* ap=ui.accessPoint(i);
    drawUiIcon(c,17,y+6,ap && ap->secured?UiIcon::Lock:UiIcon::Wifi,enabled?mint:muted);
    drawWifiLiteral(c,40,y+5,ui.rowSsid(i),enabled?cream:muted,30);
    char detail[32];
    if(ui.savedRow(i))copy(detail,sizeof(detail),state.state==State::Connected && !strcmp(state.currentSsid,ui.rowSsid(i))?"SAVED - CONNECTED":"SAVED - SELECT TO CONNECT");
    else if(!ap->supported)copy(detail,sizeof(detail),"SECURITY NOT SUPPORTED");
    else snprintf(detail,sizeof(detail),"%d DBM  %s",static_cast<int>(ap->rssi),ap->secured?"PASSWORD":"OPEN");
    c.text(40,y+18,detail,muted);
  }
  if(ui.maxScroll()){c.rect(233,listTop,2,listBottom-listTop,muted);c.rect(233,listTop+ui.scrollOffset()*82/ui.maxScroll(),2,20,gold);}
  const char* labels[]={"RESCAN","MANUAL","FORGET","BACK"};
  const UiIcon icons[]={UiIcon::Wifi,UiIcon::Keyboard,UiIcon::Delete,UiIcon::Back};
  for(int i=0;i<4;++i)button(c,12+(i%2)*112,184+(i/2)*28,104,labels[i],ui.focus()==WifiNetworksUi::kScan+i,ui.targetEnabled(WifiNetworksUi::kScan+i),icons[i]);
}
} // namespace sloth
