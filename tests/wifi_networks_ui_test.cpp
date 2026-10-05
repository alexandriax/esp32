#include "../firmware/sloth_pet/wifi_networks_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace sloth;
static int key(char c) {
  for(int i=0;i<WifiNetworkEditor::kKeyCount;++i)if(WifiNetworkEditor::keyCharacter(i)==c)return i;
  assert(false);return -1;
}
static void type(WifiNetworksUi& ui,const char* text) {
  for(;*text;++text)assert(ui.activate(key(*text))==WifiNetworksEvent::Changed);
}
static void render(const WifiNetworksUi& ui) {
  std::vector<uint16_t> pixels(240*240+2,0xabcd);drawWifiNetworks(pixels.data()+1,ui);
  assert(pixels.front()==0xabcd && pixels.back()==0xabcd);
  for(int y=-1;y<=240;++y)for(int x=-1;x<=240;++x) {
    const int hit=ui.hitTest(x,y);assert(hit==-1 || ui.targetEnabled(hit));
  }
}
static wifi_networks::Snapshot scan() {
  wifi_networks::Snapshot s={};s.state=wifi_networks::State::Ready;s.count=12;
  s.hasSaved=true;strcpy(s.savedSsid,"Home WiFi");
  for(int i=0;i<12;++i){snprintf(s.networks[i].ssid,33,"Network %02d",i);s.networks[i].rssi=-30-i*5;s.networks[i].secured=i!=0;s.networks[i].supported=i!=3;}
  return s;
}
int main() {
  WifiNetworksUi ui;assert(!ui.isOpen());render(ui);
  auto s=scan();ui.show(s);assert(ui.rowCount()==13 && ui.savedRow(0));render(ui);
  assert(ui.activate(0)==WifiNetworksEvent::ConnectSaved);
  assert(!ui.password()[0] && !ui.ssid()[0]); // Saved secrets never enter the UI.
  // A scanned AP matching the saved name reuses saved credentials, not empty text.
  strcpy(s.networks[0].ssid,"Home WiFi");ui.update(s);assert(ui.rowCount()==12);
  assert(ui.activate(0)==WifiNetworksEvent::ConnectSaved);
  assert(!ui.targetEnabled(3)); // Unsupported AP remains visible but cannot join.
  assert(ui.activate(3)==WifiNetworksEvent::None);
  // A secured AP cannot connect until a valid password has been entered.
  assert(ui.activate(1)==WifiNetworksEvent::Changed);
  assert(ui.page()==WifiNetworksPage::Credentials && !ui.editor().validNetwork());
  assert(!strcmp(ui.ssid(),"Network 01"));render(ui);
  ui.activate(WifiNetworkEditor::kPassword);type(ui,"sensitive");ui.activate(WifiNetworkEditor::kDone);
  assert(ui.activate(WifiNetworkEditor::kConnect)==WifiNetworksEvent::ConnectNetwork);
  assert(!strcmp(ui.password(),"sensitive"));
  // An unrelated scan update must never discard entered credentials mid-edit.
  s.generation++;strcpy(s.networks[1].ssid,"Reordered");ui.update(s);
  assert(!strcmp(ui.ssid(),"Network 01") && !strcmp(ui.password(),"sensitive"));
  ui.credentialsSubmitted();assert(ui.page()==WifiNetworksPage::Networks && !ui.password()[0] && !ui.ssid()[0]);
  // Forget is explicit and starts with Cancel focused.
  ui.activate(WifiNetworksUi::kForget);assert(ui.page()==WifiNetworksPage::ConfirmForget);
  assert(ui.focus()==WifiNetworksUi::kCancelForget);render(ui);
  assert(ui.activate()==WifiNetworksEvent::Changed);
  ui.activate(WifiNetworksUi::kForget);assert(ui.activate(WifiNetworksUi::kConfirmForget)==WifiNetworksEvent::Forget);
  // Touch drags scroll with bounds and never activate the original row.
  ui.touch(true,30,90);ui.touch(true,30,-1000);assert(ui.scrollOffset()==ui.maxScroll());
  assert(ui.touch(false,30,100)==WifiNetworksEvent::None);render(ui);
  ui.touch(true,30,100);ui.touch(true,30,1500);assert(ui.scrollOffset()==0);ui.touch(false,30,100);
  // A new list invalidates an in-flight press, so release cannot choose a new SSID.
  ui.touch(true,30,90);strcpy(s.savedSsid,"Replacement");ui.update(s);
  assert(ui.touch(false,30,90)==WifiNetworksEvent::None);
  for(int i=0;i<80;++i){ui.next();assert(ui.targetEnabled(ui.focus()));render(ui);}
  // Handoff/scanning/joining status disables operations; Back stays responsive.
  ui.setWaiting(true);assert(ui.targetEnabled(WifiNetworksUi::kBack));
  assert(ui.activate(WifiNetworksUi::kScan)==WifiNetworksEvent::None);
  assert(ui.activate(0)==WifiNetworksEvent::None);render(ui);
  ui.setWaiting(false);s.state=wifi_networks::State::Scanning;ui.update(s);render(ui);
  assert(!ui.targetEnabled(WifiNetworksUi::kManual));
  s.state=wifi_networks::State::Joining;strcpy(s.currentSsid,"Replacement");ui.update(s);render(ui);
  s.state=wifi_networks::State::Connected;strcpy(s.address,"192.168.1.20");ui.update(s);render(ui);
  assert(ui.targetEnabled(WifiNetworksUi::kScan));
  for(auto error:{wifi_networks::Error::Busy,wifi_networks::Error::InvalidCredentials,wifi_networks::Error::Radio,wifi_networks::Error::Scan,wifi_networks::Error::Join,wifi_networks::Error::Timeout,wifi_networks::Error::Disconnected,wifi_networks::Error::Storage}) {
    s.state=wifi_networks::State::Failed;s.error=error;ui.update(s);render(ui);
  }
  ui.setNotice("Please try again");render(ui);
  // A corrupt record can still be explicitly forgotten even without a usable SSID.
  auto broken=s;broken.hasSaved=false;broken.savedSsid[0]=0;
  broken.state=wifi_networks::State::Ready;broken.error=wifi_networks::Error::Storage;
  ui.show(broken);assert(ui.targetEnabled(WifiNetworksUi::kForget));render(ui);
  ui.activate(WifiNetworksUi::kForget);render(ui);
  assert(ui.activate(WifiNetworksUi::kConfirmForget)==WifiNetworksEvent::Forget);
  ui.show(s);
  // Manual entry is the shared editor; Back cancels field drafts then the editor.
  ui.activate(WifiNetworksUi::kManual);ui.activate(WifiNetworkEditor::kSsid);type(ui,"NewNet");
  ui.back();assert(!ui.ssid()[0]);ui.back();assert(ui.page()==WifiNetworksPage::Networks);
  ui.activate(WifiNetworksUi::kManual);ui.activate(WifiNetworkEditor::kSsid);type(ui,"OpenNet");ui.activate(WifiNetworkEditor::kDone);
  assert(ui.activate(WifiNetworkEditor::kConnect)==WifiNetworksEvent::ConnectNetwork);
  ui.close();assert(!ui.ssid()[0] && !ui.password()[0] && !ui.isOpen());render(ui);
  // Raw/UTF-8 scanned SSID bytes are preserved; unsupported font bytes render '?'.
  s={};s.state=wifi_networks::State::Ready;s.count=1;s.networks[0].supported=true;
  strcpy(s.networks[0].ssid,"Caf\xc3\xa9");ui.show(s);ui.activate(0);
  assert(!strcmp(ui.ssid(),"Caf\xc3\xa9") && ui.editor().validNetwork());render(ui);
  // Losing radio ownership while editing returns to the waiting status and wipes drafts.
  ui.activate(WifiNetworkEditor::kPassword);type(ui,"private");ui.setWaiting(true);
  assert(ui.page()==WifiNetworksPage::Networks && !ui.password()[0] && !ui.ssid()[0]);
  assert(ui.activate(WifiNetworksUi::kManual)==WifiNetworksEvent::None);render(ui);
  // Defensive snapshot clamping keeps even a malformed count bounded.
  s=scan();s.count=255;ui.show(s);assert(ui.rowCount()<=13);render(ui);
  s={};ui.show(s);assert(ui.rowCount()==0 && ui.maxScroll()==0);render(ui);
  assert(ui.activate(WifiNetworksUi::kScan)==WifiNetworksEvent::Scan);
  assert(ui.back()==WifiNetworksEvent::Back);
  puts("Wi-Fi UI: shared credentials, saved/open/secure selection, raw SSIDs, bounded scroll, stale presses, status, confirmation and render bounds passed");
}
