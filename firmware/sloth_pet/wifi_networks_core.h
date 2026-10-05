#pragma once
#include "wifi_networks.h"
#include "wifi_network_store.h"

namespace sloth { namespace wifi_networks { namespace detail {
using wifi_network_store::Credentials;
using wifi_network_store::Result;
enum class Link { Joining, Connected, Failed, Down };
struct Driver {
  virtual ~Driver() {}
  virtual bool scanStart()=0;
  virtual int scanResult()=0; // -1 pending, -2 error, otherwise available count.
  virtual bool accessPoint(unsigned index,AccessPoint& ap)=0;
  virtual bool join(const Credentials& credentials)=0;
  virtual Link link()=0;
  virtual void connection(char address[16],int16_t& rssi)=0;
  virtual void stop()=0; // Complete driver / scan-handler cleanup before return.
};
struct Store {
  virtual ~Store() {}
  virtual Result load(Credentials& credentials)=0;
  virtual bool save(const Credentials& credentials)=0;
  virtual bool forget()=0;
};
class Controller {
 public:
  Controller(Driver& driver,Store& store);
  ~Controller();
  bool scan(uint32_t now);
  bool connect(const char* ssid,const char* password,bool persist,uint32_t now);
  bool connectSaved(uint32_t now);
  void tick(uint32_t now);
  void stop();
  bool forgetSaved();
  bool busy()const;
  bool connected()const{return info_.state==State::Connected;}
  const Snapshot& snapshot()const{return info_;}
 private:
  void refreshSaved();
  void transition(State state,Error error=Error::None);
  void fail(Error error);
  Driver& driver_;
  Store& store_;
  Snapshot info_{};
  Credentials pending_{};
  uint32_t started_=0,lastMetrics_=0;
  bool persist_=false;
};
} } }
