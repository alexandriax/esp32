#include "wifi_networks_core.h"
#include <string.h>
namespace sloth { namespace wifi_networks { namespace detail {
namespace {
void wipe(void* bytes,size_t size){volatile uint8_t* p=static_cast<volatile uint8_t*>(bytes);while(size--)*p++=0;}
bool credentialsValid(const char* ssid,const char* password){
  if(!ssid||!password)return false;
  const size_t n=strnlen(ssid,33),p=strnlen(password,65);
  if(!n||n>32||p>64||(p>0&&p<8))return false;
  if(p==64)for(size_t i=0;i<p;++i)if(!((password[i]>='0'&&password[i]<='9')||(password[i]>='a'&&password[i]<='f')||(password[i]>='A'&&password[i]<='F')))return false;
  return true;
}
}
Controller::Controller(Driver& driver,Store& store):driver_(driver),store_(store){}
Controller::~Controller(){wipe(&pending_,sizeof(pending_));}
void Controller::transition(State state,Error error){info_.state=state;info_.error=error;++info_.generation;}
void Controller::refreshSaved(){
  Credentials c{};const Result result=store_.load(c);
  info_.hasSaved=result==Result::Ok;memset(info_.savedSsid,0,sizeof(info_.savedSsid));
  if(info_.hasSaved)memcpy(info_.savedSsid,c.ssid,sizeof(info_.savedSsid));
  wipe(&c,sizeof(c));
  if(result==Result::Invalid||result==Result::StorageError)info_.error=Error::Storage;
}
bool Controller::busy()const{return info_.state==State::Scanning||info_.state==State::Joining||info_.state==State::Connected;}
void Controller::stop(){
  if(busy())driver_.stop();
  wipe(&pending_,sizeof(pending_));persist_=false;
  memset(info_.currentSsid,0,sizeof(info_.currentSsid));memset(info_.address,0,sizeof(info_.address));info_.rssi=0;
  transition(State::Off);
}
void Controller::fail(Error error){driver_.stop();wipe(&pending_,sizeof(pending_));persist_=false;memset(info_.address,0,sizeof(info_.address));transition(State::Failed,error);}
bool Controller::scan(uint32_t now){
  stop();refreshSaved();info_.count=0;memset(info_.networks,0,sizeof(info_.networks));
  if(!driver_.scanStart()){fail(Error::Radio);return false;}
  started_=now;transition(State::Scanning,info_.error);return true;
}
bool Controller::connect(const char* ssid,const char* password,bool persist,uint32_t now){
  if(!credentialsValid(ssid,password)){stop();transition(State::Failed,Error::InvalidCredentials);return false;}
  Credentials next{};memcpy(next.ssid,ssid,strlen(ssid)+1);memcpy(next.password,password,strlen(password)+1);
  stop();refreshSaved();pending_=next;wipe(&next,sizeof(next));persist_=persist;
  memcpy(info_.currentSsid,pending_.ssid,sizeof(info_.currentSsid));
  if(!driver_.join(pending_)){fail(Error::Radio);return false;}
  started_=now;transition(State::Joining);return true;
}
bool Controller::connectSaved(uint32_t now){
  Credentials c{};const Result result=store_.load(c);
  if(result!=Result::Ok){stop();refreshSaved();transition(State::Failed,result==Result::Missing?Error::InvalidCredentials:Error::Storage);return false;}
  const bool accepted=connect(c.ssid,c.password,true,now);wipe(&c,sizeof(c));return accepted;
}
void Controller::tick(uint32_t now){
  if(info_.state==State::Scanning){
    const int count=driver_.scanResult();
    if(count==-2){fail(Error::Scan);return;}
    if(count>=0){
      for(int i=0;i<count;++i){
        AccessPoint ap{};if(!driver_.accessPoint(static_cast<unsigned>(i),ap))continue;
        ap.ssid[32]=0;if(!ap.ssid[0])continue;
        unsigned at=0;
        for(;at<info_.count;++at)if(strcmp(info_.networks[at].ssid,ap.ssid)==0&&info_.networks[at].secured==ap.secured&&info_.networks[at].supported==ap.supported)break;
        if(at<info_.count){if(info_.networks[at].rssi<ap.rssi)info_.networks[at]=ap;}
        else if(info_.count<kMaxNetworks)info_.networks[info_.count++]=ap;
        else {at=0;for(unsigned j=1;j<info_.count;++j)if(info_.networks[j].rssi<info_.networks[at].rssi)at=j;if(ap.rssi>info_.networks[at].rssi)info_.networks[at]=ap;}
      }
      for(unsigned i=0;i<info_.count;++i)for(unsigned j=i+1;j<info_.count;++j)if(info_.networks[j].rssi>info_.networks[i].rssi){const AccessPoint ap=info_.networks[i];info_.networks[i]=info_.networks[j];info_.networks[j]=ap;}
      driver_.stop();transition(State::Ready,info_.error);return;
    }
    if(static_cast<uint32_t>(now-started_)>=15000)fail(Error::Timeout);
  }else if(info_.state==State::Joining){
    const Link link=driver_.link();
    if(link==Link::Connected){
      if(persist_&&!store_.save(pending_)){fail(Error::Storage);return;}
      wipe(&pending_,sizeof(pending_));persist_=false;refreshSaved();
      driver_.connection(info_.address,info_.rssi);info_.address[15]=0;lastMetrics_=now;transition(State::Connected);
    }else if(link==Link::Failed)fail(Error::Join);
    else if(static_cast<uint32_t>(now-started_)>=20000)fail(Error::Timeout);
  }else if(info_.state==State::Connected){
    if(driver_.link()!=Link::Connected){fail(Error::Disconnected);return;}
    if(static_cast<uint32_t>(now-lastMetrics_)>=1000){char address[16]{};int16_t rssi=0;driver_.connection(address,rssi);address[15]=0;lastMetrics_=now;if(strcmp(address,info_.address)||rssi!=info_.rssi){memcpy(info_.address,address,sizeof(address));info_.rssi=rssi;++info_.generation;}}
  }
}
bool Controller::forgetSaved(){
  stop();if(!store_.forget()){transition(State::Failed,Error::Storage);return false;}
  refreshSaved();++info_.generation;return true;
}
} } }
