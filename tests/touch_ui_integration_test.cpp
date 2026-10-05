#include "Arduino.h"
#include "Wire.h"
#include "sensors.h"
#include "browser_ui.h"
#include "wifi_networks_ui.h"
#include "remote_display_ui.h"
#include "settings_ui.h"
#include "menu_ui.h"
#include "game_overlay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

unsigned elapsedDelay=0;
SerialMock Serial;
WireMock Wire;

// Each poll starts with new zeroed output variables, exactly as pollInputs does.
// Finger-up packets supply neither a useful point nor a useful coordinate.
template<class Ui> auto sample(Ui& ui,bool down,int x,int y,int releaseKind=0)
    -> decltype(ui.touch(down,x,y)) {
  memset(Wire.touch,0,sizeof(Wire.touch));Wire.touch[6]=0xab;
  if(down) {
    const unsigned rx=479-2*x,ry=2*y;
    Wire.touch[0]=6;Wire.touch[5]=1;
    Wire.touch[1]=ry>>4;Wire.touch[2]=rx>>4;Wire.touch[3]=((ry&15)<<4)|(rx&15);
  } else if(releaseKind) {
    Wire.touch[5]=1; // Cleared status, stale count and unrelated coordinates.
    Wire.touch[1]=7;Wire.touch[2]=11;Wire.touch[3]=0xff;
  }
  uint16_t px=0,py=0;
  const auto status=sensors::readTouch(px,py);
  assert(status==(down?sensors::TouchStatus::Pressed:sensors::TouchStatus::Released));
  return ui.touch(down,px/2,py/2);
}
template<class Ui> void drag(Ui& ui,int from,int to) {
  sample(ui,true,120,from);sample(ui,true,120,to);sample(ui,false,0,0);
}
template<class Ui> bool point(const Ui& ui,int target,int& x,int& y) {
  int left=240,right=-1,top=240,bottom=-1;
  for(int py=0;py<240;++py)for(int px=0;px<240;++px)if(ui.hitTest(px,py)==target) {
    if(px<left)left=px;if(px>right)right=px;
    if(py<top)top=py;if(py>bottom)bottom=py;
  }
  if(right<0)return false;
  x=(left+right)/2;y=(top+bottom)/2;
  if(ui.hitTest(x,y)!=target) {
    // Back can have separate rail and footer regions. Choose a real region.
    for(y=0;y<240;++y)for(x=0;x<240;++x)if(ui.hitTest(x,y)==target)return true;
    assert(false);
  }
  return true;
}
template<class Ui> auto tap(Ui& ui,int target,int releaseKind=0)
    -> decltype(ui.touch(false,0,0)) {
  int x=0,y=0;
  if(!point(ui,target,x,y)) {
    // Reach the key through swipes, never Next/Select. Search from the top.
    for(int i=0;i<24;++i)drag(ui,110,165);
    for(int i=0;i<20 && !point(ui,target,x,y);++i)drag(ui,165,110);
  }
  assert(point(ui,target,x,y));
  sample(ui,true,x,y);return sample(ui,false,0,0,releaseKind);
}
static void browser() {
  using namespace sloth;
  BrowserUi ui;ui.show();
  tap(ui,BrowserUi::kUrl);assert(ui.page()==BrowserPage::Url);
  tap(ui,BrowserUi::kClear);assert(!ui.draft()[0]);
  for(int i=0;i<BrowserUi::kKeyCount;++i) {
    assert(tap(ui,i,i%2).type==BrowserEventType::Changed);
    assert(ui.draft()[0]==BrowserUi::keyCharacter(i) && ui.draft()[1]==0);
    tap(ui,BrowserUi::kDelete);assert(!ui.draft()[0]);
  }
  tap(ui,0);assert(tap(ui,BrowserUi::kDone).type==BrowserEventType::Navigate);
  tap(ui,BrowserUi::kUrl);assert(ui.page()==BrowserPage::Url);
  tap(ui,BrowserUi::kNavBack);assert(ui.page()==BrowserPage::Page);
  ui.setHasPage(true);ui.setCanGoBack(true);
  sample(ui,true,100,160);auto event=sample(ui,true,100,110);
  assert(event.type==BrowserEventType::Scroll && event.delta==100);
  assert(sample(ui,false,0,0).type==BrowserEventType::None);
  sample(ui,true,100,100);event=sample(ui,false,0,0,1);
  assert(event.type==BrowserEventType::Link && event.x==200 && event.y==(100-BROWSER_PAGE_TOP)*2);
  assert(tap(ui,BrowserUi::kReload).type==BrowserEventType::Reload);
  ui.setLoading(true);assert(tap(ui,BrowserUi::kReload).type==BrowserEventType::Cancel);
  ui.setLoading(false);
  assert(tap(ui,BrowserUi::kWifi).type==BrowserEventType::Wifi);
  assert(tap(ui,BrowserUi::kBack).type==BrowserEventType::Back);
  assert(tap(ui,BrowserUi::kExit).type==BrowserEventType::RequestExit);
}
static void wifi() {
  using namespace sloth;
  wifi_networks::Snapshot state={};state.state=wifi_networks::State::Ready;
  WifiNetworksUi ui;ui.show(state);
  tap(ui,WifiNetworksUi::kManual);assert(ui.page()==WifiNetworksPage::Credentials);
  for(int field=0;field<2;++field) {
    tap(ui,field);assert(ui.editor().page()==(field?WifiEditorPage::Password:WifiEditorPage::Ssid));
    for(int i=0;i<WifiNetworkEditor::kKeyCount;++i) {
      assert(tap(ui,i,i%2)==WifiNetworksEvent::Changed);
      assert(ui.editor().editBuffer()[0]==WifiNetworkEditor::keyCharacter(i) && ui.editor().editBuffer()[1]==0);
      tap(ui,WifiNetworkEditor::kDelete);assert(!ui.editor().editBuffer()[0]);
    }
    tap(ui,WifiNetworkEditor::kNavBack);
    assert(ui.editor().page()==WifiEditorPage::Network);
  }
  tap(ui,WifiNetworkEditor::kNetworkBack);assert(ui.page()==WifiNetworksPage::Networks);
  assert(tap(ui,WifiNetworksUi::kScan)==WifiNetworksEvent::Scan);
  assert(tap(ui,WifiNetworksUi::kBack)==WifiNetworksEvent::Back);
  // Remote Display delegates its keyboard to the same shared credential editor.
  RemoteUi remote;remote.show(RemoteStatus::Ready,true);remote.editNetwork("Synthetic");
  tap(remote,RemoteUi::kPassword);tap(remote,0);assert(!strcmp(remote.editBuffer(),"a"));
  tap(remote,RemoteUi::kNavBack);tap(remote,RemoteUi::kNetworkBack);
}
static void namesAndMenu() {
  using namespace sloth;
  SettingsUi settings;Settings saved;settings.open(saved);
  tap(settings,SettingsUi::kMainName);
  while(settings.nameBuffer()[0])tap(settings,SettingsUi::kNameDelete);
  for(int i=0;i<26;++i) {
    tap(settings,i,i%2);assert(settings.nameBuffer()[0]=='A'+i && settings.nameBuffer()[1]==0);
    tap(settings,SettingsUi::kNameDelete);
  }
  tap(settings,SettingsUi::kNameToggle);assert(settings.keyboardNumeric());
  const char* numbers="1234567890-'";
  for(int i=0;i<12;++i) {
    tap(settings,i,i%2);assert(settings.nameBuffer()[0]==numbers[i] && settings.nameBuffer()[1]==0);
    tap(settings,SettingsUi::kNameDelete);
  }
  tap(settings,SettingsUi::kNavBack);assert(settings.page()==SettingsPage::Main);
  GameOverlay score;score.enterName(GameKind::Pong,1,0,"MOSS");
  for(int i=0;i<36;++i) {
    tap(score,i,i%2);assert(strlen(score.name())==1);
    tap(score,38);assert(!score.name()[0]);
  }
  MenuUi menu;menu.open();drag(menu,170,60);assert(menu.scrollOffset()==menu.maxScroll());
  assert(tap(menu,MenuUi::kMainSettings)==MenuEvent::OpenSettings);
  assert(tap(menu,MenuUi::kBack)==MenuEvent::Closed);
}
int main() {
  Wire.present[0x5a]=true;assert(sensors::begin().touch);
  browser();wifi();namesAndMenu();
  puts("Hardware touch packets: URL bar, all browser/Wi-Fi/name keys, controls, swipes and release coordinates passed");
}
