#include "browser_app.h"
#include "browser_ui.h"
#include "browser_fetch.h"
#include "browser_url.h"
#include "browser_engine.h"
#include "browser_assets.h"
#include "browser_reader.h"
#include "browser_memory.h"
#include "wifi_networks.h"
#include "board.h"
#include "pet_rtc.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>
#include <new>
#include <string.h>
#include <stdio.h>
#include "browser_stripe_sink.h"

namespace browser_fetch=sloth::browser_fetch;
namespace browser_url=sloth::browser_url;
namespace {
using Phase=browser_app::Phase;
bool writeStripe(void*,unsigned,unsigned,unsigned,const uint8_t*,size_t);
struct Browser {
  moss_lhp::StripeSink sink{writeStripe,nullptr};
  sloth::BrowserUi ui;
  browser_fetch::Result document{};
  char pending[512]{}, loaded[512]{}, history[4][512]{};
  unsigned historyCount=0,readerScroll=0;
  // Eight logical rows match the board's 16-native-row DMA stripe. Reuse this
  // for chrome, keyboard and reader; never allocate a whole browser frame.
  uint16_t stripe[240*8];
  uint32_t readerPaintUs=0,readerPaints=0;
  Phase phase=Phase::Idle;
  browser_app::Destination destination=browser_app::Destination::None;
  browser_app::Destination exitDestination=browser_app::Destination::Menu;
  bool queued=false, dirty=true, ntp=false, editorVisible=false;
  bool requireClock=false, clockRetried=false;
  uint32_t clockSince=0;
  browser_fetch::Snapshot lastFetch{browser_fetch::State::Idle,browser_fetch::Error::None,0,0};
  BrowserEngineError lastEngineError=BROWSER_ENGINE_ERROR_NONE;
  uint32_t lastEnginePeak=0;
  bool trackFetch=false, trackEngine=false;
};
Browser* app=nullptr;
bool writeStripe(void*,unsigned y,unsigned width,unsigned height,const uint8_t* bytes,size_t size) {
  return board::presentRegion(0,y,width,height,bytes,size);
}

void dirty(){if(app)app->dirty=true;}
void status(const char* text){app->ui.setStatus(text);dirty();}
void stopClock(){if(app->ntp){esp_sntp_stop();app->ntp=false;}}
void discardFetch(){browser_fetch::Result discarded{};if(browser_fetch::take(discarded))browser_fetch::release(discarded);}
void dropPage(){
  if(app->trackEngine){
    app->lastEnginePeak=static_cast<uint32_t>(browser_engine_peak_bytes());
    const auto error=browser_engine_error();
    if(error!=BROWSER_ENGINE_ERROR_NONE)app->lastEngineError=error;
    app->trackEngine=false;
  }
  browser_engine_stop();app->sink.cancel();browser_fetch::release(app->document);app->ui.setHasPage(false);
}
void stopWork(){
  browser_fetch::cancel();
  if(app->trackFetch)app->lastFetch=browser_fetch::snapshot();
  dropPage();app->phase=Phase::Cancelling;
  app->ui.setLoading(app->queued || app->destination!=browser_app::Destination::None);dirty();
}
void fail(const char* message){
  dropPage();sloth::wifi_networks::stop();stopClock();app->phase=Phase::Idle;
  app->trackFetch=false;app->ui.setLoading(false);status(message);
}
bool scrollPage(int y,bool force=false){
  if(app->document.reader){
    const int height=app->document.reader->height();const int maximum=height>static_cast<int>(BROWSER_VIEW_HEIGHT)?height-BROWSER_VIEW_HEIGHT:0;
    const unsigned target=static_cast<unsigned>(y<0?0:y>maximum?maximum:y)&~1u;
    if(!force&&target==app->readerScroll)return false;
    app->readerScroll=target;app->phase=Phase::Render;return true;
  }
  if(browser_engine_scroll(y)!=0)return false;app->phase=Phase::Render;return true;
}
unsigned scrollPosition(){return app->document.reader?app->readerScroll:browser_engine_scroll_y();}
void navigate(const char* input,bool remember){
  char normalized[512];
  if(!browser_url::normalize(input,normalized)){
    if(app->ui.hasPage() && scrollPage(scrollPosition(),true)){
      app->ui.setUrl(app->loaded);app->phase=Phase::Render;
    }
    status("INVALID OR UNSUPPORTED URL");return;
  }
  if(remember && app->loaded[0] && strcmp(app->loaded,normalized)){
    if(app->historyCount==4){memmove(app->history,app->history+1,3*sizeof(app->history[0]));--app->historyCount;}
    strcpy(app->history[app->historyCount++],app->loaded);
  }
  strcpy(app->pending,normalized);app->ui.setUrl(normalized);app->clockRetried=false;
  app->ui.setCanGoBack(app->historyCount!=0);app->queued=true;stopWork();
  app->lastFetch={browser_fetch::State::Idle,browser_fetch::Error::None,0,0};
  app->lastEngineError=BROWSER_ENGINE_ERROR_NONE;app->lastEnginePeak=0;app->trackFetch=false;
  status("PREPARING PAGE...");
}
const char* fetchError(browser_fetch::Error error){
  using E=browser_fetch::Error;
  switch(error){
    case E::Http:
      if(app->lastFetch.httpStatus==429)return "HTTP 429 - WAIT, THEN RELOAD";
      if(app->lastFetch.httpStatus==403)return "HTTP 403 - WEBSITE DENIED ACCESS";
      if(app->lastFetch.httpStatus==404)return "HTTP 404 - PAGE NOT FOUND";
      return "WEBSITE RETURNED AN HTTP ERROR";
    case E::Clock:return "CLOCK NOT READY - TRY RELOAD";
    case E::Memory:return "NOT ENOUGH MEMORY FOR THIS PAGE";
    case E::Tls:return "HTTPS CERTIFICATE / TLS FAILED";
    case E::TooLarge:return app->ui.textMode()?"SOURCE EXCEEDS 512 KB LIMIT":"PAGE EXCEEDS 32 KB LIMIT";
    case E::ContentType:return "LINK IS NOT AN HTML PAGE OR FEED";
    case E::Encoding:return "PAGE ENCODING IS UNSUPPORTED";
    case E::Redirect:return "REDIRECT LIMIT OR UNSAFE REDIRECT";
    case E::Timeout:return "PAGE LOAD TIMED OUT";
    case E::Dns:return "HOST NAME COULD NOT BE FOUND";
    case E::Connect:return "COULD NOT CONNECT TO WEBSITE";
    case E::Url:return "INVALID OR UNSUPPORTED URL";
    default:return "PAGE COULD NOT BE LOADED";
  }
}
bool clockValid(){const time_t utc=time(nullptr);return utc>=1704067200 && utc<4102444800LL;}
void prepareClock(uint32_t now,bool required){
  app->phase=Phase::Clock;app->clockSince=now;app->requireClock=required;
  if(required && !clockValid()){
    uint32_t utc=0;
    if(pet_rtc::read(utc)==pet_rtc::Status::Valid && utc>=1704067200 && utc<4102444800UL){
      timeval clock={static_cast<time_t>(utc),0};settimeofday(&clock,nullptr);
    }
    if(!clockValid()){
      configTime(0,0,"pool.ntp.org","time.cloudflare.com");app->ntp=true;
      status("SETTING CLOCK FOR HTTPS...");
    }
  }
}
void handle(sloth::BrowserEvent event){
  if(!app || app->phase==Phase::Closed || event.type==sloth::BrowserEventType::None)return;
  using E=sloth::BrowserEventType;
  if(event.type==E::Navigate)navigate(app->ui.url(),true);
  else if(event.type==E::Mode)navigate(app->ui.url(),false);
  else if(event.type==E::Reload)navigate(app->ui.url(),false);
  else if(event.type==E::Cancel){app->queued=false;stopWork();status("CANCELLING...");}
  else if(event.type==E::Exit)browser_app::close(app->exitDestination);
  else if(event.type==E::RequestExit)browser_app::requestClose();
  else if(event.type==E::Wifi)browser_app::requestClose(browser_app::Destination::Wifi);
  else if(event.type==E::Back){
    if(app->historyCount){char previous[512];strcpy(previous,app->history[--app->historyCount]);navigate(previous,false);}
    else browser_app::requestClose();
  }else if(event.type==E::Scroll && (app->phase==Phase::Ready || app->phase==Phase::Render)){
    const int y=static_cast<int>(scrollPosition())+event.delta;
    scrollPage(y);return; // A drag changes content, not the chrome.
  }else if(event.type==E::Link && app->phase==Phase::Ready){
    char link[512],resolved[512];
    int found=0;
    if(app->document.reader){
      const int id=app->document.reader->linkAt(event.x,event.y+app->readerScroll);
      const char* target=app->document.reader->link(id);if(target){strcpy(link,target);found=1;}
    }else found=browser_engine_link_at(event.x,event.y,link,sizeof(link));
    if(found>0){
      if(browser_url::resolve(app->loaded,link,resolved))navigate(resolved,true);
      else status("LINK TYPE OR URL NOT SUPPORTED");
    }else if(found<0)status("LINK URL IS TOO LONG");
    else return;
  }else if(event.type==E::Changed){
    if(app->ui.page()==sloth::BrowserPage::Url && !app->editorVisible && app->ui.loading()){
      app->queued=false;stopWork();
    }else if(app->ui.page()==sloth::BrowserPage::Page && app->editorVisible && app->ui.hasPage()){
      browser_engine_resume();
      if(scrollPage(scrollPosition(),true))app->phase=Phase::Render;
    }
  }
  app->editorVisible=app->ui.page()!=sloth::BrowserPage::Page;
  dirty();
}
// Build each stripe immediately before the synchronous board transfer. A text
// viewport completes in this loop turn: later touch samples cannot keep
// restarting a partly painted page, and no delay(5) separates its stripes.
bool paintRows(unsigned first,unsigned end,bool reader){
  const auto assets=sloth::browser_assets::callbacks(app->document.assets);
  for(unsigned y=first;y<end;){
    const unsigned height=end-y<8?end-y:8;
    for(unsigned row=0;row<height;++row){
      if(reader)sloth::browser_reader::drawRow(app->stripe+row*240,*app->document.reader,app->readerScroll/2+y+row-BROWSER_PAGE_TOP,&assets);
      else sloth::drawBrowserRow(app->stripe+row*240,app->ui,y+row);
    }
    if(!board::presentRegion(0,y,240,height,reinterpret_cast<const uint8_t*>(app->stripe),height*480,2))return false;
    y+=height;yield();
  }
  return true;
}
void paintUi(){
  if(!app->dirty)return;app->dirty=false;
  const bool chrome=app->ui.page()==sloth::BrowserPage::Page&&app->ui.hasPage();
  if(chrome){if(!paintRows(0,BROWSER_PAGE_TOP,false)||!paintRows(BROWSER_PAGE_BOTTOM,240,false))app->dirty=true;}
  else if(!paintRows(0,240,false))app->dirty=true;
}
}
extern "C" int browser_panel_begin(){return app && app->sink.begin(BROWSER_PAGE_TOP*2,BROWSER_VIEW_HEIGHT);}
extern "C" int browser_panel_line(unsigned y,const uint8_t* rgb,size_t bytes){return app && app->sink.row(y,rgb,bytes);}
extern "C" int browser_panel_finish(){return app && app->sink.finish();}
namespace browser_app {
bool open(const char* url,bool load){
  if(app)return false;
  app=new(std::nothrow) Browser;
  if(!app)return false;
  app->ui.show(url);
  status("READER - TAP READ FOR HTML");
  if(load)navigate(app->ui.url(),false);
  return true;
}
bool active(){return app!=nullptr;}
Snapshot snapshot(){
  Snapshot result;
  if(!app)return result;
  result.active=true;result.phase=app->phase;
  result.page=static_cast<uint8_t>(app->ui.page());result.focus=static_cast<int16_t>(app->ui.focus());
  result.loading=app->ui.loading();result.hasPage=app->ui.hasPage();
  result.historyCount=static_cast<uint8_t>(app->historyCount);
  const auto fetch=app->trackFetch && (app->phase==Phase::Fetch || app->phase==Phase::Cancelling)?
    browser_fetch::snapshot():app->lastFetch;
  result.fetchState=static_cast<uint8_t>(fetch.state);result.fetchError=static_cast<uint8_t>(fetch.error);
  result.httpStatus=fetch.httpStatus;result.received=fetch.received;
  const auto error=app->trackEngine?browser_engine_error():app->lastEngineError;
  result.engineError=static_cast<uint8_t>(error);
  result.enginePeak=app->trackEngine?static_cast<uint32_t>(browser_engine_peak_bytes()):app->lastEnginePeak;
  result.engineLive=static_cast<uint32_t>(browser_engine_live_bytes());
  const auto assets=sloth::browser_assets::stats(app->document.assets);
  result.images=assets.images;result.styles=assets.styles;result.cacheHits=assets.hits;result.skipped=assets.skipped>255?255:assets.skipped;result.sdCache=assets.sd;
  result.textMode=app->ui.textMode();result.readerPaintUs=app->readerPaintUs;result.readerPaints=app->readerPaints;
  if(app->document.reader){result.lines=app->document.reader->count;result.links=app->document.reader->links;result.scroll=app->readerScroll;result.contentHeight=app->document.reader->height();}
  else if(app->trackEngine){
    result.scroll=static_cast<uint16_t>(browser_engine_scroll_y());
    result.contentHeight=static_cast<uint16_t>(browser_engine_content_height());
  }
  return result;
}
const char* url(){return app?app->ui.url():"";}
void requestClose(Destination destination){
  if(!app || app->destination!=Destination::None)return;
  app->exitDestination=destination;app->ui.requestExit();app->editorVisible=true;dirty();
}
void close(Destination destination){
  if(!app)return;
  app->destination=destination;app->queued=false;stopWork();status("CLOSING BROWSER...");
}
Destination finished(){return app && app->phase==Phase::Closed?app->destination:Destination::None;}
void destroy(){if(app && app->phase==Phase::Closed){delete app;app=nullptr;}}
void next(){if(app){app->ui.next();dirty();}}
void activate(){if(app)handle(app->ui.activate());}
void back(){if(app)handle(app->ui.back());}
void touch(bool down,int x,int y){if(app)handle(app->ui.touch(down,x,y));}
void cancelTouch(){if(app)app->ui.cancelTouch();}
void tick(uint32_t now){
  if(!app)return;
  if(app->trackFetch && (app->phase==Phase::Fetch || app->phase==Phase::Cancelling))
    app->lastFetch=browser_fetch::snapshot();
  if(app->phase==Phase::Cancelling && !browser_fetch::busy()){
    discardFetch();sloth::wifi_networks::stop();stopClock();
    if(app->destination!=Destination::None)app->phase=Phase::Closed;
    else if(app->queued){
      app->queued=false;
      if(!sloth::wifi_networks::hasSaved()){
        app->destination=Destination::Wifi;app->phase=Phase::Closed;
      }else if(!sloth::wifi_networks::connectSaved())fail("WI-FI SETUP FAILED - CHOOSE WI-FI");
      else{app->phase=Phase::Joining;status("CONNECTING TO SAVED WI-FI...");}
    }else{app->phase=Phase::Idle;app->ui.setLoading(false);status("LOAD STOPPED");}
  }
  if(app->phase==Phase::Joining){
    sloth::wifi_networks::tick();const auto wifi=sloth::wifi_networks::snapshot();
    if(wifi.state==sloth::wifi_networks::State::Failed)fail("WI-FI FAILED - CHOOSE WI-FI");
    else if(wifi.state==sloth::wifi_networks::State::Connected){
      prepareClock(now,strncmp(app->pending,"https://",8)==0);
    }
  }
  if(app->phase==Phase::Clock){
    if(!app->requireClock || clockValid()){
      stopClock();
      app->trackFetch=true;
      const bool started=browser_fetch::begin(app->pending,app->ui.textMode());
      app->lastFetch=browser_fetch::snapshot();
      if(started){app->phase=Phase::Fetch;status("LOADING PAGE... STOP TO CANCEL");}
      else fail("COULD NOT START PAGE LOAD");
    }else if(static_cast<uint32_t>(now-app->clockSince)>=20000)fail("CLOCK SYNC FAILED - TRY RELOAD");
  }
  if(app->phase==Phase::Fetch && !browser_fetch::busy()){
    const auto result=browser_fetch::snapshot();app->lastFetch=result;
    if(result.state==browser_fetch::State::Complete && browser_fetch::take(app->document)){
      app->trackFetch=false;
      // Sequential peaks: TLS is gone before take; stop Wi-Fi before LHP layout.
      sloth::wifi_networks::stop();
      app->ui.setUrl(app->document.url);strcpy(app->loaded,app->document.url);
      if(app->document.reader){
        app->readerScroll=0;app->phase=Phase::Render;app->ui.setHasPage(true);status("DRAWING TEXT...");
      }else{
      const size_t freeBytes=heap_caps_get_free_size(MALLOC_CAP_8BIT)+browser_memory_capacity()-browser_memory_live();
      const size_t reserve=48u*1024u;
      size_t budget=freeBytes>reserve?freeBytes-reserve:0;
      if(budget>128u*1024u)budget=128u*1024u;
      if(budget<24u*1024u){
        app->lastEngineError=BROWSER_ENGINE_ERROR_MEMORY;fail("PAGE DOES NOT FIT AVAILABLE MEMORY");
      }else{
        app->trackEngine=true;
        const auto assets=sloth::browser_assets::callbacks(app->document.assets);
        if(browser_engine_start_assets(app->document.bytes,app->document.length,budget,&assets)!=0)
          fail("PAGE DOES NOT FIT AVAILABLE MEMORY");
        else{app->phase=Phase::Render;app->ui.setHasPage(true);status("LAYING OUT PAGE...");}
      }
    }
    }else if(result.error==browser_fetch::Error::Clock && !app->clockRetried){
      // An HTTP starting URL may redirect to HTTPS. Synchronize once and retry
      // the original address while retaining our established Wi-Fi connection.
      app->clockRetried=true;prepareClock(now,true);
    }else fail(fetchError(result.error));
  }
  paintUi();
  if(app->phase==Phase::Render && app->ui.page()==sloth::BrowserPage::Page){
    auto state=BROWSER_ENGINE_READY;
    if(app->document.reader){
      const uint32_t started=micros();
      if(!paintRows(BROWSER_PAGE_TOP,BROWSER_PAGE_BOTTOM,true)){fail("DISPLAY WRITE FAILED");return;}
      app->readerPaintUs=micros()-started;++app->readerPaints;
    }else state=browser_engine_service();
    if(state==BROWSER_ENGINE_READY){
      app->phase=Phase::Ready;
      const bool wasLoading=app->ui.loading();app->ui.setLoading(false);
      if(wasLoading)status(app->document.reader?(app->document.reader->clipped?"TEXT CLIPPED - FOLLOW LINKS FOR MORE":"READER - DRAG / TAP BLUE LINKS"):browser_engine_clipped()?"LONG PAGE CLIPPED - SCROLL TO READ":
        app->document.secure?"HTTPS - SCROLL / TAP A LINK":"HTTP (UNENCRYPTED) - TAP A LINK");
      if(wasLoading && app->document.assets){
        const auto a=sloth::browser_assets::stats(app->document.assets);char message[40];
        if(!a.sd)status("IMAGES NEED SD - CACHE UNAVAILABLE");
        else{snprintf(message,sizeof(message),"%s: %u IMAGES %u SKIPPED",app->document.reader?"READ":"HTML",a.images,a.skipped);status(message);}
      }
    }else if(state==BROWSER_ENGINE_FAILED)fail("PAGE TOO COMPLEX OR RENDER FAILED");
  }
}
}
