#include "../firmware/sloth_pet/remote_display_ui.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
using namespace sloth;
static int key(char c) { for(int i=0;i<RemoteUi::kKeyCount;++i)if(RemoteUi::keyCharacter(i)==c)return i;assert(false);return -1; }
static void type(RemoteUi& ui,const char* text) { for(;*text;++text) assert(ui.activate(key(*text))==RemoteEvent::Changed); }
static void password(RemoteUi& ui,const char* text) { ui.activate(RemoteUi::kPassword);type(ui,text);ui.activate(RemoteUi::kDone); }
static void render(const RemoteUi& ui) {
  std::vector<uint16_t> pixels(240*240+2,0xabcd);drawRemoteDisplay(pixels.data()+1,ui);
  assert(pixels.front()==0xabcd && pixels.back()==0xabcd);
  for(int y=-1;y<=240;++y)for(int x=-1;x<=240;++x) {
    const int hit=ui.hitTest(x,y);assert(hit==-1 || ui.targetEnabled(hit));
  }
}
int main() {
  RemoteUi ui;assert(!ui.isOpen() && ui.targetCount()==0);render(ui);
  for(auto status:{RemoteStatus::Connecting,RemoteStatus::NoHost,RemoteStatus::NoUSB,RemoteStatus::Failed,RemoteStatus::PermissionNeeded, RemoteStatus::Ready}) {
    ui.show(status,false);assert(!ui.targetEnabled(RemoteUi::kUseUSB));render(ui);
    ui.show(status,true);assert(ui.activate(RemoteUi::kUseUSB)==RemoteEvent::UseUSB);render(ui);
  }
  assert(ui.activate(RemoteUi::kRetry)==RemoteEvent::Retry);
  assert(ui.activate(RemoteUi::kEditNetwork)==RemoteEvent::EditNetwork);
  assert(ui.page()==RemotePage::Network && !ui.validNetwork());render(ui);
  ui.activate(RemoteUi::kSsid);assert(!ui.targetEnabled(RemoteUi::kDone));
  bool seen[128]={};
  for(int i=0;i<RemoteUi::kKeyCount;++i) {
    const unsigned c=static_cast<unsigned char>(RemoteUi::keyCharacter(i));assert(c>=32 && c<=126 && !seen[c]);seen[c]=true;
    assert(ui.activate(i)==RemoteEvent::Changed);assert(static_cast<unsigned char>(ui.editBuffer()[0])==c);
    ui.activate(RemoteUi::kDelete);
  }
  for(int c=32;c<=126;++c)assert(seen[c]);
  type(ui,"AbC space!\\`~[]{}");ui.back();assert(!ui.ssid()[0]); // Transactional cancel.
  ui.activate(RemoteUi::kSsid);type(ui,"CaseSensitiveNetwork");ui.activate(RemoteUi::kDone);
  assert(strcmp(ui.ssid(),"CaseSensitiveNetwork")==0 && ui.validNetwork());
  password(ui,"short");assert(!ui.validNetwork());
  ui.activate(RemoteUi::kPassword);for(int i=0;i<5;++i)ui.activate(RemoteUi::kDelete);
  for(int i=0;i<63;++i)assert(ui.activate(key('a'))==RemoteEvent::Changed);
  assert(ui.activate(key('b'))==RemoteEvent::None && strlen(ui.editBuffer())==63);
  render(ui);ui.activate(RemoteUi::kDone);assert(ui.validNetwork() && strlen(ui.password())==63);
  assert(ui.activate(RemoteUi::kConnect)==RemoteEvent::ConnectNetwork);
  ui.activate(RemoteUi::kSsid);for(size_t i=strlen(ui.editBuffer());i;--i)ui.activate(RemoteUi::kDelete);
  for(int i=0;i<32;++i)ui.activate(key('s'));
  assert(ui.activate(key('x'))==RemoteEvent::None && strlen(ui.editBuffer())==32);ui.activate(RemoteUi::kDone);
  // Password never appears on-screen: equal-length secrets produce equal frames.
  RemoteUi other;other.editNetwork(ui.ssid());password(other,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  ui.activate(RemoteUi::kPassword);other.activate(RemoteUi::kPassword);
  std::vector<uint16_t> a(240*240),b(240*240);drawRemoteDisplay(a.data(),ui);drawRemoteDisplay(b.data(),other);assert(a==b);
  ui.show(RemoteStatus::NoUSB,false);assert(!ui.ssid()[0] && !ui.password()[0] && !ui.editBuffer()[0]);
  ui.editNetwork("WiFi");ui.activate(RemoteUi::kPassword);
  ui.touch(true,22,110);ui.touch(true,22,-2000);assert(ui.scrollOffset()==ui.maxScroll());
  assert(ui.touch(false,22,160)==RemoteEvent::None && !ui.editBuffer()[0]);render(ui);
  for(int i=0;i<200;++i){ui.next();render(ui);} // Hardware cycles through all enabled keys/footer.
  ui.back();assert(ui.page()==RemotePage::Network);ui.back();assert(ui.page()==RemotePage::Status);
  assert(ui.back()==RemoteEvent::Back);ui.close();assert(!ui.ssid()[0] && !ui.password()[0]);
  ui.editNetwork("WiFi");ui.activate(RemoteUi::kSsid);ui.touch(true,22,103);ui.cancelTouch();
  assert(ui.touch(false,22,103)==RemoteEvent::None && strcmp(ui.editBuffer(),"WiFi")==0);
  // Rail acts on existing focus, never joins keyboard focus or inserts a key on drag.
  assert(ui.hitTest(40,15)==RemoteUi::kNavNext && ui.hitTest(120,15)==RemoteUi::kNavBack && ui.hitTest(190,15)==RemoteUi::kNavSelect);
  ui.touch(true,40,15);assert(ui.touch(false,40,15)==RemoteEvent::Changed && ui.focus()==1);
  ui.touch(true,190,15);assert(ui.touch(false,190,15)==RemoteEvent::Changed);
  assert(strcmp(ui.editBuffer(),"WiFib")==0);
  ui.touch(true,120,15);ui.touch(true,190,15);assert(ui.touch(false,120,15)==RemoteEvent::None);
  assert(ui.page()==RemotePage::Ssid && ui.focus()==1);
  ui.touch(true,120,15);assert(ui.touch(false,120,15)==RemoteEvent::Changed);
  assert(ui.page()==RemotePage::Network && strcmp(ui.ssid(),"WiFi")==0);
  ui.activate(RemoteUi::kNavBack);assert(ui.page()==RemotePage::Status);
  assert(ui.activate(RemoteUi::kNavBack)==RemoteEvent::Back);
  ui.close();assert(ui.activate(RemoteUi::kNavNext)==RemoteEvent::None && ui.hitTest(40,15)==-1);
  puts("remote display UI: ASCII keyboard, field limits, masking, validation, drafts, navigation, touch scroll and render bounds passed");
}
