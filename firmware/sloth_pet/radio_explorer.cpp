#include <new>
#include "radio_explorer.h"
#include "pet_canvas.h"
#include "navigation_bar.h"
#include "ui_icons.h"
#include <cmath>
#include <stdio.h>
#include <string.h>

namespace sloth {
namespace {
constexpr int kTop=52, kBottom=196, kRow=36;
bool same(const RadioEntry& a,const RadioEntry& b) { return a.addressType==b.addressType && !memcmp(a.address,b.address,6); }
int clamp(int v,int lo,int hi) { return v<lo?lo:v>hi?hi:v; }
bool inside(int x,int y,int l,int t,int w,int h) { return x>=l && x<l+w && y>=t && y<t+h; }
bool fresh(uint32_t now,uint32_t then,uint32_t limit) { return now-then<=limit; }
void clipped(char* out,size_t capacity,const char* input,unsigned width) {
  unsigned n=0;while(input[n] && n<width && n+1<capacity){out[n]=input[n];++n;}out[n]=0;
  if(input[n] && n>=3){out[n-3]='.';out[n-2]='.';out[n-1]='.';}
}
const char* entryName(const RadioEntry& e,RadioKind kind) { return e.name[0]?e.name:kind==RadioKind::Wifi?"HIDDEN NETWORK":"UNNAMED DEVICE"; }
}
void radioName(char* out,size_t capacity,const uint8_t* data,size_t length) {
  if(!out||!capacity)return;
  size_t n=0;
  if(data)for(;n<length && n+1<capacity && data[n];++n){
    const unsigned c=data[n];
    out[n]=(c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
        c==' '||c=='.'||c==','||c=='!'||c=='?'||c==':'||c=='\''||c=='-'||c=='/'||c=='%'?static_cast<char>(c):'?';
  }
  out[n]=0;
}
void radioAddress(char* out,size_t capacity,const uint8_t address[6]) {
  if(!out||!capacity)return;
  if(!address){out[0]=0;return;}
  snprintf(out,capacity,"%02X:%02X:%02X:%02X:%02X:%02X",address[0],address[1],address[2],address[3],address[4],address[5]);
}
const char* radioSecurity(uint8_t security) {
  const char* names[]={"OPEN","WEP","WPA","WPA2","WPA/WPA2","WPA2 ENTERPRISE","WPA3","WPA2/WPA3","WAPI","OWE","WPA3 ENT 192","WPA3","WPA3","DPP","WPA3 ENTERPRISE","WPA2/WPA3 ENT","WPA ENTERPRISE"};
  return security<sizeof(names)/sizeof(names[0])?names[security]:"UNKNOWN SECURITY";
}
void radioAdvertising(RadioEntry& entry,const uint8_t* bytes,size_t length) {
  entry.advertisingLength=static_cast<uint8_t>(bytes?(length>64?64:length):0);
  entry.advertisingTruncated=length>64;
  memset(entry.advertising,0,sizeof(entry.advertising));
  if(entry.advertisingLength)memcpy(entry.advertising,bytes,entry.advertisingLength);
  if(!bytes)return;
  for(size_t pos=0;pos<length;){
    const size_t size=bytes[pos++];
    if(!size || size>length-pos)break;
    const uint8_t type=bytes[pos];
    if((type==9 || (type==8 && !entry.name[0])) && size>1)radioName(entry.name,sizeof(entry.name),bytes+pos+1,size-1);
    if(type==10 && size==2)entry.txPower=static_cast<int8_t>(bytes[pos+1]);
    pos+=size;
  }
}
void radioServiceSummary(const RadioEntry& entry,char* out,size_t capacity) {
  if(!out||!capacity)return;
  snprintf(out,capacity,"%s",entry.advertisingTruncated?"UUID NOT IN ADV PREVIEW":"NO UUID ADVERTISED");
  const auto* bytes=entry.advertising;
  for(size_t pos=0;pos<entry.advertisingLength;){
    const size_t size=bytes[pos++];if(!size||size>entry.advertisingLength-pos)break;
    const uint8_t type=bytes[pos];
    if((type==2||type==3||type==0x16) && size>=3){
      snprintf(out,capacity,"UUID16 %04X",unsigned(bytes[pos+1])|(unsigned(bytes[pos+2])<<8));return;
    }
    if((type==4||type==5||type==0x20) && size>=5){
      const uint32_t id=uint32_t(bytes[pos+1])|(uint32_t(bytes[pos+2])<<8)|(uint32_t(bytes[pos+3])<<16)|(uint32_t(bytes[pos+4])<<24);
      snprintf(out,capacity,"UUID32 %08lX",static_cast<unsigned long>(id));return;
    }
    if((type==6||type==7||type==0x21) && size>=17){snprintf(out,capacity,"128-BIT UUID IN ADV DATA");return;}
    pos+=size;
  }
}
void radioObserve(RadioSnapshot& snapshot,const RadioEntry& entry) {
  if(entry.rssi < -126 || entry.rssi > 20)return;
  unsigned i=0;for(;i<snapshot.count && i<kRadioCapacity;++i)if(same(snapshot.entries[i],entry))break;
  if(i>=kRadioCapacity){++snapshot.dropped;return;}
  RadioEntry value=entry;value.name[32]=0;
  if(value.advertisingLength>64){value.advertisingLength=64;value.advertisingTruncated=true;}
  if(i<snapshot.count){
    const auto& old=snapshot.entries[i];
    value.observations=old.observations==UINT32_MAX?UINT32_MAX:old.observations+1;
    if(!value.name[0])memcpy(value.name,old.name,sizeof(value.name));
    if(!value.advertisingLength && old.advertisingLength){
      memcpy(value.advertising,old.advertising,sizeof(value.advertising));value.advertisingLength=old.advertisingLength;
      value.advertisingTruncated=old.advertisingTruncated;
    }
    if(value.txPower==127)value.txPower=old.txPower;
  }else{value.observations=1;snapshot.count=static_cast<uint8_t>(i+1);}
  snapshot.entries[i]=value;++snapshot.generation;
}
void RadioSweep::reset(){headingCount_=headingHead_=pointCount_=pointHead_=0;angle_=0;motionAt_=lastSeen_=0;motionReady_=haveSeen_=false;}
void RadioSweep::motion(float gx,float gy,float gz,float ax,float ay,float az,uint32_t now){
  const float norm=std::sqrt(ax*ax+ay*ay+az*az);
  const float speed=std::sqrt(gx*gx+gy*gy+gz*gz);
  if(!std::isfinite(norm)||!std::isfinite(speed)||norm<0.8f||norm>1.2f||speed>180||std::fabs(az)/norm>0.65f){
    motionReady_=false;headingCount_=headingHead_=0;return;
  }
  const uint32_t dt=now-motionAt_;
  if(!motionReady_||dt>150){angle_=0;pointCount_=pointHead_=0;headingCount_=headingHead_=0;}
  else {
    // Accelerometer points up at rest. Right-handed positive rotation about up
    // is counterclockwise; negate for the user's clockwise/right sweep.
    const float yaw=-(gx*ax+gy*ay+gz*az)/norm;
    if(std::fabs(yaw)>1.5f)angle_+=yaw*(static_cast<float>(dt)/1000.0f);
    if(std::fabs(angle_)>180){angle_=0;pointCount_=pointHead_=0;headingCount_=headingHead_=0;}
  }
  motionAt_=now;motionReady_=true;
  headings_[headingHead_].angle=angle_;headings_[headingHead_].at=now;
  headingHead_=(headingHead_+1)%96;if(headingCount_<96)++headingCount_;
}
void RadioSweep::observe(int rssi,uint32_t seenAt){
  if(rssi < -126 || rssi > 20 || (haveSeen_ && seenAt==lastSeen_))return;
  haveSeen_=true;lastSeen_=seenAt;
  float angle=std::nanf("");uint32_t nearest=101;
  for(unsigned n=0;n<headingCount_;++n){
    const auto& h=headings_[(headingHead_+96-1-n)%96];
    const uint32_t before=seenAt-h.at,after=h.at-seenAt;
    const uint32_t distance=before<after?before:after;
    if(distance<nearest){nearest=distance;angle=h.angle;}
  }
  auto& p=points_[pointHead_];p.angle=angle;p.rssi=static_cast<int16_t>(rssi);p.at=seenAt;
  pointHead_=(pointHead_+1)%24;if(pointCount_<24)++pointCount_;
}
RadioHint RadioSweep::hint(uint32_t now)const{
  RadioHint result;result.motionReady=motionReady_&&fresh(now,motionAt_,250);
  result.fresh=haveSeen_&&fresh(now,lastSeen_,2000);
  float sx=0,sy=0,sxx=0,syy=0,sxy=0,lo=10000,hi=-10000,lastAngle=0;
  unsigned left=0,right=0;int oldest=0,newest=0;bool haveTrend=false,haveAngle=false;
  for(unsigned i=0;i<pointCount_;++i){
    const auto& p=points_[(pointHead_+24-pointCount_+i)%24];
    if(!fresh(now,p.at,10000))continue;
    if(!haveTrend){oldest=p.rssi;haveTrend=true;}newest=p.rssi;
    if(!std::isfinite(p.angle))continue;
    if(haveAngle){if(p.angle-lastAngle>1)++right;if(p.angle-lastAngle < -1)++left;}
    lastAngle=p.angle;haveAngle=true;
    sx+=p.angle;sy+=p.rssi;sxx+=p.angle*p.angle;syy+=p.rssi*p.rssi;sxy+=p.angle*p.rssi;
    if(p.angle<lo)lo=p.angle;if(p.angle>hi)hi=p.angle;++result.samples;
  }
  if(result.fresh&&haveTrend)result.trend=newest-oldest;
  if(result.samples)result.sweepDegrees=hi-lo;
  if(result.samples<10||!result.fresh||!result.motionReady||right<2||left<2||hi-lo<25)return result;
  const float n=static_cast<float>(result.samples),xx=sxx-sx*sx/n,yy=syy-sy*sy/n,xy=sxy-sx*sy/n;
  if(xx<1||yy<1)return result;
  result.correlation=xy/std::sqrt(xx*yy);
  const float slope=xy/xx,residual=std::sqrt(std::fmax(0.0f,(yy-xy*xy/xx)/n));
  if(std::fabs(result.correlation)>=0.80f && std::fabs(slope)*(hi-lo)>=6 && residual<=3)
    result.direction=slope>0?1:-1;
  return result;
}
struct RadioExplorerUi::Content {RadioSnapshot snapshot;RadioSweep sweep;};
RadioExplorerUi::~RadioExplorerUi(){close();}
size_t RadioExplorerUi::retainedBytes()const{return content_?sizeof(Content):0;}
const RadioSnapshot& RadioExplorerUi::snapshot()const{static const RadioSnapshot empty{};return content_?content_->snapshot:empty;}
RadioHint RadioExplorerUi::hint(uint32_t now)const{return content_?content_->sweep.hint(now):RadioHint{};}
void RadioExplorerUi::open(RadioKind kind){close();content_=new(std::nothrow)Content;if(!content_)return;open_=true;content_->snapshot=RadioSnapshot();content_->snapshot.kind=kind;content_->snapshot.state=RadioScanState::Starting;page_=RadioPage::List;focus_=kRefresh;scroll_=0;selected_=-1;releaseRequired_=false;}
void RadioExplorerUi::close(){open_=false;cancelTouch();delete content_;content_=nullptr;page_=RadioPage::List;selected_=-1;scroll_=0;}
const RadioEntry* RadioExplorerUi::selected()const{return content_&&page_==RadioPage::Detail&&selected_>=0&&selected_<content_->snapshot.count?&content_->snapshot.entries[selected_]:nullptr;}
void RadioExplorerUi::update(const RadioSnapshot& view,uint32_t now){
  (void)now;if(!open_||view.kind!=content_->snapshot.kind)return;
  RadioEntry previous;const bool had=selected()!=nullptr;if(had)previous=*selected();
  content_->snapshot=view;if(content_->snapshot.count>kRadioCapacity)content_->snapshot.count=kRadioCapacity;
  for(unsigned i=0;i<content_->snapshot.count;++i){content_->snapshot.entries[i].name[32]=0;if(content_->snapshot.entries[i].advertisingLength>64)content_->snapshot.entries[i].advertisingLength=64;}
  if(had){selected_=-1;for(unsigned i=0;i<content_->snapshot.count;++i)if(same(previous,content_->snapshot.entries[i])){selected_=static_cast<int>(i);break;}
    if(selected_<0){page_=RadioPage::List;focus_=kRefresh;scroll_=0;content_->sweep.reset();}
  }
  if(selected())content_->sweep.observe(selected()->rssi,selected()->seenAt);
  if(page_==RadioPage::List && focus_>=0&&focus_<int(kRadioCapacity)&&focus_>=content_->snapshot.count)focus_=kRefresh;
  if(scroll_>maxScroll())scroll_=maxScroll();
}
void RadioExplorerUi::motion(float gx,float gy,float gz,float ax,float ay,float az,uint32_t now){if(selected())content_->sweep.motion(gx,gy,gz,ax,ay,az,now);}
int RadioExplorerUi::maxScroll()const{
  if(!content_)return 0;
  int content=content_->snapshot.count*kRow;
  if(page_==RadioPage::Detail){
    const int rows=selected()?(selected()->advertisingLength+7)/8:0;
    const int last=content_->snapshot.kind==RadioKind::Bluetooth?268+rows*13+25:320;
    content=last+12-kTop;
  }
  return content>kBottom-kTop?content-(kBottom-kTop):0;
}
void RadioExplorerUi::reveal(){if(page_==RadioPage::List&&focus_>=0&&focus_<content_->snapshot.count){const int top=focus_*kRow;if(top<scroll_)scroll_=top;else if(top+32>scroll_+kBottom-kTop)scroll_=top+32-(kBottom-kTop);scroll_=clamp(scroll_,0,maxScroll());}}
void RadioExplorerUi::next(){
  if(!open_)return;cancelTouch();
  if(page_==RadioPage::Detail)focus_=focus_==kMore?kRefresh:focus_==kRefresh?kBack:kMore;
  else if(focus_==kRefresh)focus_=kBack;else if(focus_==kBack)focus_=content_->snapshot.count?0:kRefresh;
  else focus_=focus_+1<content_->snapshot.count?focus_+1:kRefresh;
  reveal();
}
RadioEvent RadioExplorerUi::activate(int target){
  if(!open_)return RadioEvent::None;
  if(target==kNavNext){next();return RadioEvent::Changed;}
  if(target==kNavBack)return back();
  if(target==-1||target==kNavSelect)target=focus_;
  if(target==kBack)return back();
  if(target==kRefresh){cancelTouch();content_->sweep.reset();return RadioEvent::Refresh;}
  if(page_==RadioPage::Detail&&target==kMore){cancelTouch();scroll_=scroll_>=maxScroll()?0:clamp(scroll_+120,0,maxScroll());focus_=kMore;return RadioEvent::Changed;}
  if(page_==RadioPage::List&&target>=0&&target<content_->snapshot.count){cancelTouch();selected_=target;page_=RadioPage::Detail;scroll_=0;focus_=kMore;content_->sweep.reset();return RadioEvent::SelectionChanged;}
  return RadioEvent::None;
}
RadioEvent RadioExplorerUi::back(){if(!open_)return RadioEvent::None;cancelTouch();if(page_==RadioPage::Detail){page_=RadioPage::List;focus_=selected_>=0?selected_:kRefresh;selected_=-1;scroll_=0;content_->sweep.reset();reveal();return RadioEvent::SelectionChanged;}return RadioEvent::Back;}
int RadioExplorerUi::hitTest(int x,int y)const{
  if(!open_||x<0||x>=240||y<0||y>=240)return -1;
  if(inside(x,y,20,8,200,16))return x<86?kNavNext:x<154?kNavBack:kNavSelect;
  if(y>=204&&y<228){
    if(page_==RadioPage::Detail){if(x>=20&&x<83)return kMore;if(x>=89&&x<152)return kRefresh;if(x>=158&&x<221)return kBack;}
    else{if(x>=24&&x<118)return kRefresh;if(x>=124&&x<216)return kBack;}
  }
  if(page_==RadioPage::List&&inside(x,y,16,kTop,208,kBottom-kTop)){const int row=(y-kTop+scroll_)/kRow;if(row<content_->snapshot.count&&(y-kTop+scroll_)%kRow<32)return row;}
  return -1;
}
void RadioExplorerUi::cancelTouch(){if(touching_)releaseRequired_=true;touching_=dragged_=scrollGesture_=false;}
RadioEvent RadioExplorerUi::touch(bool down,int x,int y){
  if(!open_){cancelTouch();return RadioEvent::None;}
  if(releaseRequired_){if(!down)releaseRequired_=false;return RadioEvent::None;}
  if(down){
    if(!touching_){touching_=true;dragged_=false;touchX_=x;touchY_=y;touchScroll_=scroll_;touchTarget_=hitTest(x,y);scrollGesture_=inside(x,y,0,kTop,240,kBottom-kTop);}
    const int64_t dx=int64_t(x)-touchX_,dy=int64_t(y)-touchY_;
    if(dx>8||dx < -8||dy>8||dy < -8)dragged_=true;
    if(dragged_&&scrollGesture_){const int64_t offset=int64_t(touchScroll_)-dy;scroll_=offset<0?0:offset>maxScroll()?maxScroll():static_cast<int>(offset);}
    return RadioEvent::None;
  }
  if(!touching_)return RadioEvent::None;
  const int target=!dragged_&&touchTarget_==hitTest(x,y)?touchTarget_:-1;
  touching_=false;dragged_=false;scrollGesture_=false;
  return target>=0?activate(target):RadioEvent::None;
}
namespace {
using namespace graphics;
void control(Canvas& c,int x,int width,const char* text,bool focused){if(focused)c.roundRect(x-1,203,width+2,26,5,gold);c.roundRect(x,204,width,24,4,rgb(29,53,47));const int w=static_cast<int>(strlen(text))*6-1;c.text(x+(width-w)/2,212,text,cream);}
void detailText(Canvas& c,int base,int scroll,const char* text,uint16_t color){const int y=base-scroll;if(y+7>kTop&&y<kBottom)c.text(22,y,text,color);}
}
void drawRadioExplorer(uint16_t* pixels,const RadioExplorerUi& ui,uint32_t now){
  if(!pixels)return;using namespace graphics;Canvas c{pixels};c.rect(0,0,240,240,ink);if(!ui.isOpen())return;
  const auto& s=ui.snapshot();const int scroll=ui.scrollOffset();char line[48];
  if(ui.page()==RadioPage::List){
    for(unsigned i=0;i<s.count;++i){const int y=kTop+static_cast<int>(i)*kRow-scroll;if(y+32<=kTop||y>=kBottom)continue;
      if(ui.focus()==static_cast<int>(i))c.roundRect(15,y-1,210,34,5,gold);c.roundRect(16,y,208,32,4,rgb(29,53,47));
      clipped(line,sizeof(line),entryName(s.entries[i],s.kind),30);c.text(22,y+5,line,cream);
      if(!fresh(now,s.entries[i].seenAt,5000))snprintf(line,sizeof(line),"LAST SEEN %luS AGO",static_cast<unsigned long>((now-s.entries[i].seenAt)/1000));
      else if(s.kind==RadioKind::Wifi)snprintf(line,sizeof(line),"%d DBM  CH %u  %s",s.entries[i].rssi,s.entries[i].channel,radioSecurity(s.entries[i].security));
      else snprintf(line,sizeof(line),"%d DBM  PASSIVE BLE",s.entries[i].rssi);
      char shortLine[33];clipped(shortLine,sizeof(shortLine),line,32);c.text(22,y+20,shortLine,muted);
    }
    if(!s.count){c.centered(89,s.state==RadioScanState::Error?"RADIO UNAVAILABLE":"LISTENING NEARBY...",gold);c.centered(112,s.kind==RadioKind::Wifi?"2.4GHZ NETWORKS ONLY":"ADVERTISING BLE DEVICES",muted);c.centered(132,"NO CONNECTIONS OR PAIRING",muted);}
  }else if(ui.selected()){
    const auto& e=*ui.selected();const auto hint=ui.hint(now);
    char name[33];clipped(name,sizeof(name),entryName(e,s.kind),32);detailText(c,53,scroll,name,cream);
    radioAddress(line,sizeof(line),e.address);detailText(c,67,scroll,line,muted);
    snprintf(line,sizeof(line),"SIGNAL %d DBM%s",e.rssi,hint.fresh?"":" / STALE");detailText(c,84,scroll,line,cream);
    const int barY=97-scroll;if(barY+5>kTop&&barY<kBottom){c.roundRect(22,barY,196,5,2,rgb(35,64,55));c.roundRect(22,barY,clamp((e.rssi+100)*196/65,0,196),5,2,mint);}
    detailText(c,111,scroll,hint.direction>0?"STRONGER ON RIGHT":hint.direction<0?"STRONGER ON LEFT":"NO CLEAR DIRECTION",hint.direction?gold:cream);
    if(hint.direction && 107-scroll+16>kTop && 107-scroll<kBottom)drawUiIcon(c,202,107-scroll,hint.direction>0?UiIcon::Next:UiIcon::Back,gold);
    detailText(c,125,scroll,"RSSI HINT, NOT A BEARING",muted);
    detailText(c,141,scroll,"HOLD UPRIGHT. TURN SLOWLY",muted);
    detailText(c,154,scroll,"LEFT / RIGHT AND BACK",muted);
    snprintf(line,sizeof(line),"TREND %+d DB  SWEEP %.0f DEG",hint.trend,static_cast<double>(hint.sweepDegrees));detailText(c,172,scroll,line,mint);
    detailText(c,189,scroll,s.kind==RadioKind::Wifi?"NETWORK DETAILS":"ADVERTISING DETAILS",gold);
    if(s.kind==RadioKind::Wifi){
      snprintf(line,sizeof(line),"CHANNEL %u / 2.4GHZ",e.channel);detailText(c,205,scroll,line,cream);
      snprintf(line,sizeof(line),"SECURITY %s",radioSecurity(e.security));detailText(c,220,scroll,line,cream);
      detailText(c,239,scroll,"PASSIVE BEACON DISCOVERY",muted);detailText(c,253,scroll,"NOT CONNECTED TO NETWORK",muted);
      detailText(c,274,scroll,"WALLS, REFLECTIONS AND YOUR",muted);detailText(c,287,scroll,"HAND CAN CHANGE THE SIGNAL.",muted);
      detailText(c,307,scroll,"THIS IS NOT A LOCATION OR",muted);detailText(c,320,scroll,"A RELIABLE DIRECTION FINDER.",muted);
    }else{
      snprintf(line,sizeof(line),"ADDRESS %s",(e.addressType&1)?"RANDOM / MAY ROTATE":"PUBLIC");detailText(c,205,scroll,line,cream);
      if(e.txPower!=127)snprintf(line,sizeof(line),"TX POWER %d DBM",e.txPower);else snprintf(line,sizeof(line),"TX POWER NOT ADVERTISED");detailText(c,220,scroll,line,cream);
      radioServiceSummary(e,line,sizeof(line));detailText(c,235,scroll,line,cream);
      detailText(c,253,scroll,"ADVERTISING BYTES / HEX",gold);
      for(unsigned row=0;row<8;++row){unsigned length=0;line[0]=0;for(unsigned j=0;j<8 && row*8+j<e.advertisingLength;++j)length+=snprintf(line+length,sizeof(line)-length,"%02X ",e.advertising[row*8+j]);if(length)detailText(c,268+static_cast<int>(row)*13,scroll,line,muted);}
      const int end=268+((e.advertisingLength+7)/8)*13+10;
      detailText(c,end,scroll,e.advertisingTruncated?"PAYLOAD PREVIEW TRUNCATED":"PASSIVE / NO SCAN REQUESTS",muted);
      detailText(c,end+15,scroll,"ONLY ADVERTISED DATA SHOWN",muted);
    }
  }
  c.rect(0,0,240,kTop,ink);c.rect(0,kBottom,240,240-kBottom,ink);drawNavigationBar(c);
  c.centered(ui.page()==RadioPage::List?28:30,ui.page()==RadioPage::List?(s.kind==RadioKind::Wifi?"WI-FI EXPLORER":"BLE EXPLORER"):(s.kind==RadioKind::Wifi?"WI-FI DETAILS":"BLE DETAILS"),cream,2);
  if(ui.page()==RadioPage::List){
    if(s.state==RadioScanState::Error)snprintf(line,sizeof(line),"SCAN ERROR / TAP REFRESH");
    else if(s.state==RadioScanState::Stopping)snprintf(line,sizeof(line),"STOPPING RADIO...");
    else snprintf(line,sizeof(line),s.dropped?"%u FOUND / LIST LIMIT 24":"%u FOUND / PASSIVE SCAN",s.count);
    c.centered(44,line,muted);
  }
  if(ui.maxScroll()>0){const int h=kBottom-kTop,thumb=h*h/(h+ui.maxScroll());c.rect(231,kTop,3,h,rgb(35,64,55));c.rect(231,kTop+scroll*(h-thumb)/ui.maxScroll(),3,thumb,mint);}
  if(ui.page()==RadioPage::Detail){control(c,20,63,"MORE",ui.focus()==RadioExplorerUi::kMore);control(c,89,63,"SCAN",ui.focus()==RadioExplorerUi::kRefresh);control(c,158,63,"BACK",ui.focus()==RadioExplorerUi::kBack);}
  else{control(c,24,94,"REFRESH",ui.focus()==RadioExplorerUi::kRefresh);control(c,124,92,"BACK",ui.focus()==RadioExplorerUi::kBack);}
}
} // namespace sloth
