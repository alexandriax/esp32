#include "../firmware/sloth_pet/radio_explorer.h"
#include <assert.h>
#include <cmath>
#include <stdio.h>
#include <string.h>
#include <limits.h>
using namespace sloth;
static RadioEntry entry(unsigned id,int rssi=-60){RadioEntry e;e.address[5]=static_cast<uint8_t>(id);e.rssi=static_cast<int8_t>(rssi);e.seenAt=1000;snprintf(e.name,sizeof(e.name),"DEVICE %u",id);return e;}
static void metadata(){
 char name[8];const uint8_t dirty[]={'a',0xff,'_',1,'X',0,'Z'};radioName(name,sizeof(name),dirty,sizeof(dirty));assert(!strcmp(name,"a???X"));
 radioName(name,0,dirty,sizeof(dirty));radioName(nullptr,8,dirty,sizeof(dirty));radioName(name,sizeof(name),nullptr,9);assert(!*name);
 RadioEntry e;const uint8_t advertisement[]={2,1,6,5,9,'M','O','S','S',3,3,0x0f,0x18,2,10,0xfb,4,0xff,0x4c,0,0x11};
 radioAdvertising(e,advertisement,sizeof(advertisement));assert(!strcmp(e.name,"MOSS")&&e.txPower==-5);
 assert(e.advertisingLength==sizeof(advertisement)&&!e.advertisingTruncated);
 char summary[40];radioServiceSummary(e,summary,sizeof(summary));assert(!strcmp(summary,"UUID16 180F"));
 for(size_t length=0;length<=sizeof(advertisement);++length){RadioEntry truncated;radioAdvertising(truncated,advertisement,length);radioServiceSummary(truncated,summary,sizeof(summary));assert(truncated.name[32]==0);}
 uint8_t random[300];uint32_t seed=72;
 for(unsigned iteration=0;iteration<2000;++iteration){for(auto& byte:random){seed=1664525u*seed+1013904223u;byte=static_cast<uint8_t>(seed>>24);}RadioEntry malformed;radioAdvertising(malformed,random,iteration%300);radioServiceSummary(malformed,summary,sizeof(summary));assert(malformed.advertisingLength<=64&&malformed.name[32]==0);}
 radioAdvertising(e,random,sizeof(random));assert(e.advertisingTruncated&&e.advertisingLength==64);
 const uint8_t mac[]={0,1,2,0xab,0xcd,0xef};char address[18];radioAddress(address,sizeof(address),mac);assert(!strcmp(address,"00:01:02:AB:CD:EF"));
 assert(!strcmp(radioSecurity(0),"OPEN")&&!strcmp(radioSecurity(6),"WPA3")&&!strcmp(radioSecurity(255),"UNKNOWN SECURITY"));
 RadioSnapshot s;
 for(unsigned i=0;i<kRadioCapacity;++i)radioObserve(s,entry(i));assert(s.count==kRadioCapacity);
 auto updated=entry(2,-40);updated.name[0]=0;radioObserve(s,updated);assert(s.entries[2].rssi==-40&&s.entries[2].observations==2&&!strcmp(s.entries[2].name,"DEVICE 2"));
 radioObserve(s,entry(99));assert(s.count==kRadioCapacity&&s.dropped==1);
 auto bad=entry(1);bad.rssi=127;radioObserve(s,bad);assert(s.entries[1].rssi==-60);
 RadioSnapshot types;auto first=entry(1);radioObserve(types,first);first.addressType=1;radioObserve(types,first);assert(types.count==2);
}
static RadioHint sweep(bool inverted,bool noisy,bool reverse=true,uint32_t base=1000){
 RadioSweep model;float angle=0;
 for(unsigned step=0;step<=250;++step){
  const uint32_t now=base+step*20;
  const float rate=step<100?20.0f:reverse?-20.0f:20.0f;
  if(step)angle+=rate*.02f;
  model.motion(0,-rate,0,0,1,0,now);
  if(step%10==0){const int noise=noisy?(step%30==0?12:-12):0;model.observe(static_cast<int>(-70+(inverted?-angle:angle)*.4f)+noise,now);}
 }
 return model.hint(base+5000);
}
static void uncertainty(){
 auto right=sweep(false,false);assert(right.direction==1&&right.samples>=10&&right.sweepDegrees>=25&&right.correlation>.8f);
 assert(sweep(true,false).direction==-1);assert(sweep(false,true).direction==0);assert(sweep(false,false,false).direction==0);
 assert(sweep(false,false,true,UINT32_MAX-2000).direction==1);
 RadioSweep model;
 for(unsigned i=0;i<100;++i){model.motion(0,0,0,0,1,0,i*20);model.observe(-40+(i%2),i*20);}
 assert(model.hint(2000).direction==0&&model.hint(2000).sweepDegrees==0);
 model.motion(0,0,0,0,0,1,2020);assert(!model.hint(2020).motionReady); // Flat is not a guided upright sweep.
 model.motion(NAN,0,0,0,1,0,2040);assert(!model.hint(2040).motionReady);
 model.motion(0,20,0,0,3,0,2060);assert(!model.hint(2060).motionReady); // Translational acceleration rejected.
 assert(!model.hint(5000).fresh&&model.hint(5000).direction==0);
 model.reset();assert(!model.hint(5000).fresh&&model.hint(5000).samples==0);
 // RSSI still has a trend without usable gyro; no invented heading.
 model.observe(-70,100);model.observe(-65,300);assert(model.hint(300).trend==5&&model.hint(300).direction==0);
 model.observe(-10,300);assert(model.hint(300).trend==5); // Same observation cannot count twice.
}
static void uiAndRender(){
 RadioExplorerUi ui;assert(!ui.retainedBytes());assert(!ui.isOpen()&&ui.hitTest(40,60)==-1);ui.open(RadioKind::Wifi);
 RadioSnapshot snapshot;snapshot.kind=RadioKind::Wifi;snapshot.state=RadioScanState::Scanning;
 for(unsigned i=0;i<24;++i)radioObserve(snapshot,entry(i));
 ui.update(snapshot,1000);assert(ui.maxScroll()>0);
 assert(ui.touch(true,40,60)==RadioEvent::None);assert(ui.touch(false,43,63)==RadioEvent::SelectionChanged);
 assert(ui.selected()&&ui.selected()->address[5]==0&&ui.page()==RadioPage::Detail);
 assert(ui.touch(false,43,63)==RadioEvent::None);
 ui.activate(RadioExplorerUi::kMore);assert(ui.scrollOffset()>0);
 ui.activate(RadioExplorerUi::kMore);assert(ui.scrollOffset()==ui.maxScroll());
 ui.activate(RadioExplorerUi::kMore);assert(ui.scrollOffset()==0);
 ui.next();assert(ui.focus()==RadioExplorerUi::kRefresh);assert(ui.activate()==RadioEvent::Refresh);
 assert(ui.selected()&&ui.page()==RadioPage::Detail);
 ui.next();assert(ui.focus()==RadioExplorerUi::kBack);assert(ui.activate()==RadioEvent::SelectionChanged);
 assert(!ui.selected()&&ui.page()==RadioPage::List&&ui.focus()==0);
 ui.touch(true,40,180);ui.touch(true,40,-1000);assert(ui.touch(false,40,-1000)==RadioEvent::None);
 assert(ui.scrollOffset()==ui.maxScroll());assert(ui.page()==RadioPage::List);
 for(unsigned n=0;n<28;++n){ui.next();if(ui.focus()<24){bool visible=false;for(int y=52;y<196;++y)visible|=ui.hitTest(40,y)==ui.focus();assert(visible);}}
 ui.activate(0);ui.touch(true,40,214);ui.next();ui.cancelTouch();ui.touch(false,40,214);
 ui.touch(true,119,214);assert(ui.touch(false,119,214)==RadioEvent::Refresh); // Single release clears both gates.
 ui.touch(true,120,15);ui.touch(true,120,100);assert(ui.touch(false,120,15)==RadioEvent::None);
 assert(ui.page()==RadioPage::Detail);
 assert(ui.activate(RadioExplorerUi::kNavBack)==RadioEvent::SelectionChanged);
 assert(ui.back()==RadioEvent::Back);ui.close();assert(!ui.retainedBytes()&&!ui.selected()&&!ui.maxScroll());
 uint16_t guarded[240*240+2];guarded[0]=0x1234;guarded[240*240+1]=0xabcd;
 for(unsigned kind=0;kind<2;++kind){
  ui.open(static_cast<RadioKind>(kind));snapshot.kind=static_cast<RadioKind>(kind);ui.update(snapshot,1000);
  for(int detail=0;detail<2;++detail){if(detail)ui.activate(23);
   for(int offset=0;offset<5;++offset){
    ui.touch(true,100,180);ui.touch(true,100,-100);ui.touch(false,100,-100);
    drawRadioExplorer(guarded+1,ui,2000);assert(guarded[0]==0x1234&&guarded[240*240+1]==0xabcd);
    for(int y=0;y<240;++y)for(int x=0;x<240;++x){const int target=ui.hitTest(x,y);assert(target==-1||(target>=0&&target<24)||target==50||target==51||target==52||(target>=100&&target<=102));}
   }
  }
  ui.touch(true,INT_MIN,INT_MAX);ui.touch(true,INT_MAX,INT_MIN);ui.touch(false,INT_MAX,INT_MIN);
 }
 RadioSnapshot disappeared;disappeared.kind=RadioKind::Bluetooth;ui.update(disappeared,2000);assert(ui.page()==RadioPage::List&&!ui.selected());
 drawRadioExplorer(nullptr,ui,0);
}
int main(){metadata();uncertainty();uiAndRender();puts("radio_explorer: bounded discovery/AD parsing, stable identity, conservative gyro/RSSI correlation, uncertainty, gestures, paging and rendering passed");}
