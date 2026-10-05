#include "../firmware/sloth_pet/wifi_networks_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace sloth::wifi_networks;
using namespace sloth::wifi_networks::detail;
struct FakeStore : Store {
  Result result=Result::Missing; Credentials saved{}; unsigned writes=0,forgets=0;
  bool failWrite=false,failForget=false;
  Result load(Credentials& c) override {c=Credentials{};if(result==Result::Ok)c=saved;return result;}
  bool save(const Credentials& c) override {++writes;if(failWrite)return false;saved=c;result=Result::Ok;return true;}
  bool forget() override {++forgets;if(failForget)return false;saved=Credentials{};result=Result::Missing;return true;}
};
struct FakeDriver : Driver {
  bool active=false,startWorks=true,joinWorks=true;int scanned=-1;unsigned stops=0,joins=0,scans=0;
  Link status=Link::Joining;Credentials joining{};std::vector<AccessPoint> aps;
  bool scanStart() override {assert(!active);active=true;++scans;return startWorks;}
  int scanResult() override {assert(active);return scanned;}
  bool accessPoint(unsigned i,AccessPoint& ap) override {if(i>=aps.size())return false;ap=aps[i];return true;}
  bool join(const Credentials& c) override {assert(!active);active=true;++joins;joining=c;return joinWorks;}
  Link link() override {assert(active);return status;}
  void connection(char address[16],int16_t& rssi) override {strcpy(address,"192.0.2.8");rssi=-42;}
  void stop() override {active=false;++stops;joining=Credentials{};}
};
AccessPoint ap(const char* name,int rssi,bool secured=true,bool supported=true){AccessPoint a{};strncpy(a.ssid,name,32);a.rssi=rssi;a.secured=secured;a.supported=supported;return a;}
int main(){
  FakeDriver driver;FakeStore store;Controller c(driver,store);
  assert(!c.busy()&&c.snapshot().state==State::Off);
  assert(c.scan(0));assert(c.busy()&&c.snapshot().state==State::Scanning);
  c.tick(14999);assert(c.busy());c.tick(15000);assert(!c.busy()&&!driver.active&&c.snapshot().error==Error::Timeout);
  assert(c.scan(100));c.stop();assert(!c.busy()&&!driver.active);
  driver.startWorks=false;assert(!c.scan(0));assert(!driver.active&&c.snapshot().error==Error::Radio);driver.startWorks=true;
  assert(c.scan(0));driver.scanned=-2;c.tick(1);assert(c.snapshot().error==Error::Scan&&!driver.active);
  driver.aps={ap("weak",-80),ap("same",-70),ap("same",-30),ap("open",-40,false),ap("enterprise",-50,true,false),ap("",-10)};
  for(unsigned i=0;i<20;++i){char name[33];snprintf(name,sizeof(name),"AP%u",i);driver.aps.push_back(ap(name,-60-static_cast<int>(i)));}
  driver.scanned=static_cast<int>(driver.aps.size());assert(c.scan(0));c.tick(1);
  assert(c.snapshot().state==State::Ready&&!c.busy()&&!driver.active&&c.snapshot().count==12);
  assert(!strcmp(c.snapshot().networks[0].ssid,"same")&&c.snapshot().networks[0].rssi==-30);
  assert(!strcmp(c.snapshot().networks[1].ssid,"open")&&!c.snapshot().networks[1].secured);
  assert(!c.snapshot().networks[2].supported);
  for(unsigned i=1;i<c.snapshot().count;++i)assert(c.snapshot().networks[i-1].rssi>=c.snapshot().networks[i].rssi);
  assert(!c.connect("n","short",true,0));assert(c.snapshot().error==Error::InvalidCredentials&&!driver.active);
  assert(!c.connect("","",true,0));assert(!c.connect(nullptr,"password",true,0));
  char longSsid[34];memset(longSsid,'x',33);longSsid[33]=0;assert(!c.connect(longSsid,"password",true,0));
  char rawKey[65];memset(rawKey,'g',64);rawKey[64]=0;assert(!c.connect("network",rawKey,true,0));memset(rawKey,'a',64);assert(c.connect("network",rawKey,false,0));c.stop();
  // An open AP is supported without fabricating an empty WPA credential.
  assert(c.connect("open","",true,0));assert(store.writes==0);driver.status=Link::Connected;c.tick(1);
  assert(c.snapshot().state==State::Connected&&c.busy()&&store.writes==1&&store.saved.password[0]==0);
  assert(c.snapshot().hasSaved&&!strcmp(c.snapshot().savedSsid,"open")&&!strcmp(c.snapshot().address,"192.0.2.8"));
  driver.status=Link::Down;c.tick(2);assert(!c.busy()&&!driver.active&&c.snapshot().error==Error::Disconnected);
  // A failed replacement never overwrites the previous saved network.
  driver.status=Link::Joining;assert(c.connect("bad","password",true,100));c.tick(20099);assert(c.busy());c.tick(20100);
  assert(c.snapshot().error==Error::Timeout&&!driver.active&&store.writes==1&&!strcmp(store.saved.ssid,"open"));
  assert(c.connect("bad","password",true,0));driver.status=Link::Failed;c.tick(1);assert(c.snapshot().error==Error::Join&&store.writes==1&&!driver.active);
  // Join timeout remains correct over millis() wrap.
  driver.status=Link::Joining;assert(c.connect("wrap","password",true,0xfffffff0u));c.tick(0x00004e0fu);assert(c.busy());c.tick(0x00004e10u);assert(!c.busy());
  assert(c.connectSaved(0));assert(!strcmp(driver.joining.ssid,"open"));c.stop();assert(store.writes==1);
  // Cancellation/replacement releases the old scan or connection first.
  driver.scanned=-1;assert(c.scan(0));unsigned stops=driver.stops;assert(c.connect("new","password",true,0));assert(driver.stops==stops+1);
  stops=driver.stops;assert(c.scan(0));assert(driver.stops==stops+1);c.stop();
  driver.status=Link::Connected;store.failWrite=true;assert(c.connect("new","password",true,0));c.tick(1);assert(c.snapshot().error==Error::Storage&&!driver.active&&!strcmp(store.saved.ssid,"open"));store.failWrite=false;
  unsigned writes=store.writes;assert(c.connect("temporary","password",false,0));c.tick(1);assert(c.snapshot().state==State::Connected&&store.writes==writes);c.stop();
  driver.joinWorks=false;assert(!c.connect("new","password",true,0));assert(!driver.active&&c.snapshot().error==Error::Radio);driver.joinWorks=true;
  store.failForget=true;assert(!c.forgetSaved());assert(c.snapshot().error==Error::Storage);store.failForget=false;
  assert(c.forgetSaved());assert(!c.snapshot().hasSaved&&!c.snapshot().savedSsid[0]);assert(!c.connectSaved(0));
  store.result=Result::Invalid;assert(!c.connectSaved(0));assert(c.snapshot().error==Error::Storage);
  driver.scanned=0;assert(c.scan(0));assert(c.snapshot().error==Error::Storage);c.tick(1);assert(c.snapshot().state==State::Ready&&c.snapshot().error==Error::Storage);
  puts("wifi_networks: bounded scan, authentication validation, cleanup, persistence, timeouts and replacement passed");
}
