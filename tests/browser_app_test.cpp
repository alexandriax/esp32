#include "../firmware/sloth_pet/browser_app.h"
#include "../firmware/sloth_pet/browser_ui.h"
#include "../firmware/sloth_pet/browser_fetch.h"
#include "../firmware/sloth_pet/browser_engine.h"
#include "../firmware/sloth_pet/wifi_networks.h"
#include "../firmware/sloth_pet/pet_rtc.h"
#include "../firmware/sloth_pet/board.h"
#include "../firmware/sloth_pet/browser_reader.h"
#include "../firmware/sloth_pet/browser_memory.h"
#include <new>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <string>
#include <vector>

namespace bf=sloth::browser_fetch;
namespace wn=sloth::wifi_networks;
namespace {
struct Mock {
  bool textMode=true;
  bool saved=true,joinOk=true,wifiActive=false,fetchBeginOk=true,worker=false,tls=false,ntp=false;
  bool failRender=false,panelOk=true,engineActive=false,engineReady=false,paintBegun=false,clipped=false;
  int startError=0,releaseCount=0,cancelCount=0,fetchCalls=0,joinCalls=0,stopCalls=0,engineStarts=0,engineStops=0;
  unsigned regionCalls=0;
  unsigned renderRow=0,scroll=0,linkX=0,linkY=0,uiRows=0,contentRows=0,maxBoardBytes=0;
  size_t freeHeap=256*1024,budget=0,enginePeak=0,engineLive=0;
  BrowserEngineError engineError=BROWSER_ENGINE_ERROR_NONE;
  const char* engineHtml=nullptr;
  const sloth::BrowserUi* expectedUi=nullptr;
  unsigned asymmetricUiPixels=0;
  std::string request,href;
  bf::Snapshot fetch{bf::State::Idle,bf::Error::None,0,0};
  bf::Result result{};
  wn::Snapshot wifi{};
  std::vector<std::string> events;
  pet_rtc::Status rtcStatus=pet_rtc::Status::Unavailable;
  uint32_t rtc=0;
} m;
time_t fakeTime=1735689600;
uint32_t now=0;
void step(){browser_app::tick(++now);}
void tap(int x,int y){browser_app::touch(true,x,y);browser_app::touch(false,x,y);}
void editUrl(const char* text) {
  sloth::BrowserUi model;model.show(browser_app::url());model.activate(sloth::BrowserUi::kUrl);
  tap(100,15);model.activate(sloth::BrowserUi::kClear);tap(120,218);
  for(;*text;++text){
    int target=0;while(sloth::BrowserUi::keyCharacter(target)!=*text){++target;assert(target<95);}
    while(model.focus()!=target){model.next();browser_app::next();}
    model.activate();browser_app::activate();
  }
  while(model.focus()!=sloth::BrowserUi::kDone){model.next();browser_app::next();}
  browser_app::activate();
}
void reset() {
  assert(!browser_app::active());assert(!m.result.bytes && !m.engineHtml && !m.worker);
  m=Mock();fakeTime=1735689600;now=0;
}
void finishWorker(const char* url,const char* html="<html><body>Real test page</body></html>") {
  assert(m.worker && m.tls);m.events.push_back("tls-destroyed");m.tls=false;m.worker=false;
  m.fetch={bf::State::Complete,bf::Error::None,200,static_cast<uint32_t>(strlen(html))};
  assert(!m.result.bytes);m.result.bytes=static_cast<char*>(malloc(strlen(html)+1));assert(m.result.bytes);
  strcpy(m.result.bytes,html);m.result.length=strlen(html);strcpy(m.result.url,url);m.result.secure=!strncmp(url,"https://",8);
}
void cancelledWorker() {
  assert(m.worker);m.tls=false;m.worker=false;m.fetch={bf::State::Cancelled,bf::Error::Cancelled,0,0};
}
void connectWifi() {
  assert(m.wifiActive);m.wifi.state=wn::State::Connected;step();
}
void startFetch(const char* url) {
  assert(browser_app::open(url));step();assert(m.joinCalls==1 && m.wifiActive);
  connectWifi();assert(m.worker && m.request==url);
}
void ready() {
  for(int i=0;i<120 && !m.engineReady;++i)step();assert(m.engineReady);step();
}
void closeAndDestroy() {
  const auto before=browser_app::snapshot();
  browser_app::close();step();assert(browser_app::finished()==browser_app::Destination::Menu);
  const auto after=browser_app::snapshot();
  assert(after.httpStatus==before.httpStatus && after.received==before.received && after.enginePeak==before.enginePeak);
  assert(after.engineError==before.engineError && !after.engineLive);
  browser_app::destroy();assert(!browser_app::active());assert(!m.wifiActive && !m.worker && !m.engineHtml && !m.result.bytes);
}
size_t eventAt(const char* value) {
  for(size_t i=0;i<m.events.size();++i)if(m.events[i]==value)return i;
  assert(false);return 0;
}
} // namespace

// These executable-local symbols ensure the controller never reads or changes
// the host clock. No board/radio/network implementation is linked into this test.
extern "C" time_t time(time_t* value){if(value)*value=fakeTime;return fakeTime;}
extern "C" int settimeofday(const timeval* value,const struct timezone*){fakeTime=value->tv_sec;return 0;}
void yield() {}
uint32_t micros(){return now*1000;}
void configTime(long,long,const char*,const char*){m.ntp=true;m.events.push_back("ntp-start");}
void esp_sntp_stop(){m.ntp=false;m.events.push_back("ntp-stop");}
size_t heap_caps_get_free_size(uint32_t){return m.freeHeap;}
namespace pet_rtc { Status read(uint32_t& utc){utc=m.rtc;return m.rtcStatus;} }
namespace board {
void present(const uint16_t*,void (*)(),unsigned){assert(false && "Browser must never require a full framebuffer");}
bool presentRegion(unsigned x,unsigned y,unsigned width,unsigned height,const uint8_t* bytes,unsigned size,unsigned scale,bool rle,unsigned turns) {
  assert(turns == board::kConfiguredRotation);
  assert(bytes && !rle && x==0);assert(size<=3840);++m.regionCalls;if(size>m.maxBoardBytes)m.maxBoardBytes=size;
  if(scale==2){
    assert(width==240 && height>=1 && height<=8 && y+height<=240 && size==480*height);m.uiRows+=height;
    if(m.expectedUi)for(unsigned row=0;row<height;++row){
      uint16_t expected[240];sloth::drawBrowserRow(expected,*m.expectedUi,y+row);const uint8_t* pixels=bytes+row*480;
      for(unsigned column=0;column<240;++column){
        // The board API consumes LE RGB565; DMA performs the panel byte swap.
        assert(pixels[2*column]==static_cast<uint8_t>(expected[column]));
        assert(pixels[2*column+1]==static_cast<uint8_t>(expected[column]>>8));
        if(pixels[2*column]!=pixels[2*column+1])++m.asymmetricUiPixels;
      }
    }
  }else {
    assert(scale==1 && width==480 && height==2 && y>=BROWSER_PAGE_TOP*2 && y+height<=BROWSER_PAGE_BOTTOM*2 && !(y&1) && size==1920);
    // Mock renderer emits red, green, blue and white at the start of each row.
    static const uint8_t expected[]={0x00,0xf8,0xe0,0x07,0x1f,0x00,0xff,0xff};
    assert(!memcmp(bytes,expected,sizeof(expected)));
    assert(!memcmp(bytes+480*2,expected,sizeof(expected)));
    m.contentRows+=height;
  }
  return m.panelOk;
}
}
namespace sloth { namespace wifi_networks {
bool hasSaved(){return m.saved;}
bool connectSaved(){++m.joinCalls;m.events.push_back("wifi-join");if(!m.joinOk)return false;m.wifiActive=true;m.wifi.state=State::Joining;return true;}
void tick(){}
Snapshot snapshot(){return m.wifi;}
void stop(){assert(!m.worker && !m.tls && "Radio must outlive active fetch sockets");++m.stopCalls;m.events.push_back("wifi-stop");m.wifiActive=false;m.wifi.state=State::Off;}
} namespace browser_fetch {
bool begin(const char* url,bool textMode){m.textMode=textMode;++m.fetchCalls;if(!m.fetchBeginOk)return false;assert(m.wifiActive && !m.worker && !m.engineHtml && !m.result.bytes);m.events.push_back("fetch-begin");m.request=url;m.worker=m.tls=true;m.fetch={State::Running,Error::None,0,0};return true;}
void cancel(){++m.cancelCount;m.events.push_back("fetch-cancel");}
bool busy(){return m.worker;}
Snapshot snapshot(){return m.fetch;}
bool take(Result& result){assert(!m.worker && !m.tls);if(!m.result.bytes&&!m.result.reader)return false;m.events.push_back("fetch-take");result=m.result;m.result=Result();m.fetch={State::Idle,Error::None,0,0};return true;}
void release(Result& result){if(result.bytes){assert(result.bytes!=m.engineHtml && "HTML freed before engine teardown");++m.releaseCount;m.events.push_back("html-release");free(result.bytes);}if(result.reader){result.reader->~Document();browser_memory_free(result.reader);}result=Result();}
} }
extern "C" {
int browser_engine_start_assets(const char* html,size_t bytes,size_t budget,const BrowserEngineAssets*){return browser_engine_start(html,bytes,budget);}
int browser_engine_start(const char* html,size_t length,size_t budget) {
  assert(!m.worker && !m.tls && !m.wifiActive && "Layout must follow TLS and Wi-Fi teardown");
  assert(html && length==strlen(html));++m.engineStarts;m.events.push_back("engine-start");m.budget=budget;
  m.engineError=BROWSER_ENGINE_ERROR_NONE;m.enginePeak=32768;m.engineLive=16384;
  if(m.startError){m.engineError=BROWSER_ENGINE_ERROR_MEMORY;m.enginePeak=8192;m.engineLive=0;return m.startError;}
  m.engineHtml=html;m.engineActive=true;m.engineReady=m.paintBegun=false;m.renderRow=m.scroll=0;return 0;
}
BrowserEngineState browser_engine_service() {
  assert(m.engineActive && m.engineHtml && *m.engineHtml);
  if(m.failRender){m.engineError=BROWSER_ENGINE_ERROR_LAYOUT;return BROWSER_ENGINE_FAILED;}
  if(!m.paintBegun){if(!browser_panel_begin()){m.engineError=BROWSER_ENGINE_ERROR_PANEL;return BROWSER_ENGINE_FAILED;}m.paintBegun=true;}
  const uint8_t rgb[1440]={255,0,0,0,255,0,0,0,255,255,255,255};
  for(unsigned i=0;i<4 && m.renderRow<BROWSER_VIEW_HEIGHT;++i)if(!browser_panel_line(m.renderRow++,rgb,sizeof(rgb))){m.engineError=BROWSER_ENGINE_ERROR_PANEL;return BROWSER_ENGINE_FAILED;}
  if(m.renderRow==BROWSER_VIEW_HEIGHT){if(!browser_panel_finish()){m.engineError=BROWSER_ENGINE_ERROR_PANEL;return BROWSER_ENGINE_FAILED;}m.engineReady=true;return BROWSER_ENGINE_READY;}
  return BROWSER_ENGINE_PAINTING;
}
void browser_engine_resume(){}
void browser_engine_stop(){++m.engineStops;m.events.push_back("engine-stop");m.engineActive=m.engineReady=m.paintBegun=false;m.engineHtml=nullptr;m.engineLive=0;m.engineError=BROWSER_ENGINE_ERROR_NONE;}
int browser_engine_scroll(int y){assert(m.engineActive);m.events.push_back("engine-scroll");m.scroll=y<0?0:y>600?600:static_cast<unsigned>(y);m.renderRow=0;m.engineReady=m.paintBegun=false;return 0;}
int browser_engine_link_at(unsigned x,unsigned y,char* out,size_t capacity){m.linkX=x;m.linkY=y;if(m.href.empty())return 0;if(m.href.size()>=capacity)return -1;strcpy(out,m.href.c_str());return 1;}
unsigned browser_engine_scroll_y(){return m.scroll;}
unsigned browser_engine_content_height(){return 1000;}
int browser_engine_clipped(){return m.clipped;}
BrowserEngineError browser_engine_error(){return m.engineError;}
size_t browser_engine_peak_bytes(){return m.enginePeak;}
size_t browser_engine_live_bytes(){return m.engineLive;}
size_t browser_engine_failures(){return 0;}
}

int main() {
  using Phase=browser_app::Phase;
  static_assert(static_cast<unsigned>(Phase::Idle)==0 && static_cast<unsigned>(Phase::Closed)==7,"Diagnostic phase IDs are stable");
  static_assert(sizeof(browser_app::Snapshot)<=64,"Diagnostics should stay small and contain no text buffers");
  // Snapshots are passive and keep the last result after fetch ownership moves.
  reset();auto diagnostic=browser_app::snapshot();
  assert(!diagnostic.active && diagnostic.phase==Phase::Closed && diagnostic.focus==-1);
  assert(!diagnostic.engineLive && !diagnostic.enginePeak && !diagnostic.received);
  assert(browser_app::open("https://example.com/",false));
  diagnostic=browser_app::snapshot();assert(diagnostic.active && diagnostic.phase==Phase::Idle && !diagnostic.loading);
  assert(!diagnostic.hasPage && diagnostic.page==0 && diagnostic.focus==sloth::BrowserUi::kUrl);
  tap(100,15);diagnostic=browser_app::snapshot();assert(diagnostic.page==1);
  browser_app::back();tap(90,220);diagnostic=browser_app::snapshot();
  assert(diagnostic.phase==Phase::Cancelling && diagnostic.loading && !diagnostic.historyCount);
  step();assert(browser_app::snapshot().phase==Phase::Joining);connectWifi();
  m.fetch.httpStatus=200;m.fetch.received=11;
  const auto eventCount=m.events.size();diagnostic=browser_app::snapshot();
  assert(m.events.size()==eventCount && m.worker && m.tls && m.wifiActive);
  assert(diagnostic.phase==Phase::Fetch && diagnostic.fetchState==static_cast<unsigned>(bf::State::Running));
  assert(diagnostic.httpStatus==200 && diagnostic.received==11 && !diagnostic.fetchError);
  finishWorker("https://example.com/");const auto bodyBytes=m.fetch.received;step();
  diagnostic=browser_app::snapshot();assert(diagnostic.phase==Phase::Render && diagnostic.hasPage && diagnostic.loading);
  assert(diagnostic.httpStatus==200 && diagnostic.received==bodyBytes && diagnostic.fetchState==static_cast<unsigned>(bf::State::Complete));
  assert(diagnostic.enginePeak==32768 && diagnostic.engineLive==16384 && diagnostic.contentHeight==1000);
  ready();diagnostic=browser_app::snapshot();assert(diagnostic.phase==Phase::Ready && !diagnostic.loading && diagnostic.hasPage);
  browser_app::back();step();assert(browser_app::snapshot().page==2 && browser_app::snapshot().phase==Phase::Ready);
  const auto liveBeforeStay=m.engineLive;browser_app::activate();step();ready();assert(m.engineLive==liveBeforeStay);
  browser_app::touch(true,100,150);browser_app::touch(true,100,100);browser_app::touch(false,100,100);step();ready();
  assert(browser_app::snapshot().scroll==100);
  m.href="/next";tap(100,100);diagnostic=browser_app::snapshot();
  assert(diagnostic.phase==Phase::Cancelling && diagnostic.historyCount==1 && !diagnostic.hasPage);
  assert(!diagnostic.received && !diagnostic.enginePeak && !diagnostic.engineLive && !diagnostic.engineError);
  closeAndDestroy();diagnostic=browser_app::snapshot();assert(!diagnostic.active && !diagnostic.received && !diagnostic.enginePeak);

  // Actual touch keyboard commits the URL, then main-loop joins saved Wi-Fi.
  reset();assert(browser_app::open("https://example.com/",false));
  sloth::BrowserUi initialUi;initialUi.show("https://example.com/");initialUi.setStatus("READER - TAP READ FOR HTML");
  m.expectedUi=&initialUi;step();m.expectedUi=nullptr;
  assert(m.uiRows==240 && m.asymmetricUiPixels>0);
  tap(100,15);tap(20,65);tap(210,215); // edit URL, append 'a', press GO
  assert(!strcmp(browser_app::url(),"https://example.com/a"));step();
  assert(m.joinCalls==1);connectWifi();assert(m.request=="https://example.com/a");
  finishWorker("https://example.com/a");step();ready();
  assert(m.engineStarts==1 && m.budget==128*1024 && m.contentRows==BROWSER_VIEW_HEIGHT && m.maxBoardBytes==3840);
  assert(eventAt("tls-destroyed")<eventAt("fetch-take") && eventAt("fetch-take")<eventAt("engine-start"));
  // Returning from URL entry repaints the retained page without re-fetching it.
  const unsigned painted=m.contentRows;const int fetched=m.fetchCalls,freed=m.releaseCount;
  tap(80,15);step();browser_app::back();step();ready();
  assert(m.contentRows==painted+BROWSER_VIEW_HEIGHT && m.fetchCalls==fetched && m.releaseCount==freed);
  // An invalid submitted address cannot leave the keyboard painted over a page.
  const unsigned beforeInvalid=m.contentRows;editUrl("ftp://bad");step();ready();
  assert(m.fetchCalls==fetched && m.contentRows==beforeInvalid+BROWSER_VIEW_HEIGHT);
  // Swipe is a scroll, never a link activation. Coordinates belong to viewport.
  browser_app::touch(true,100,150);browser_app::touch(true,100,130);browser_app::touch(true,100,100);browser_app::touch(false,100,100);step();ready();
  assert(m.scroll==100 && m.fetchCalls==fetched);
  // Opening/cancelling the editor during a scroll repaint preserves document ownership.
  const unsigned beforeInterrupted=m.contentRows;const int beforeRelease=m.releaseCount;
  browser_app::touch(true,100,150);browser_app::touch(true,100,140);browser_app::touch(false,100,140);
  tap(80,15);step();assert(m.engineHtml && m.releaseCount==beforeRelease);
  browser_app::back();step();ready();assert(m.contentRows==beforeInterrupted+BROWSER_VIEW_HEIGHT);
  m.href="../other?from=a";tap(20,42);assert(m.linkX==40 && m.linkY==20);step();connectWifi();
  assert(m.request=="https://example.com/other?from=a" && m.releaseCount==1);
  finishWorker("https://example.com/other?from=a");step();ready();
  browser_app::back();step();connectWifi();assert(m.request=="https://example.com/a");
  finishWorker("https://example.com/a");step();ready();closeAndDestroy();

  // Reader pages own no LHP context and can repaint, scroll, link, and switch.
  reset();alignas(max_align_t) unsigned char canvas[115200];assert(browser_memory_begin(canvas,sizeof(canvas)));
  startFetch("https://news.ycombinator.com/");assert(m.textMode);m.worker=m.tls=false;
  void* storage=browser_memory_malloc(sizeof(sloth::browser_reader::Document));assert(storage);
  m.result.reader=new(storage)sloth::browser_reader::Document;strcpy(m.result.url,"https://news.ycombinator.com/");
  {sloth::browser_reader::Parser parser(*m.result.reader,false,m.result.url);const std::string page="<a href='item?id=42'>Story comments</a>"+std::string(4000,'x');parser.feed(page.data(),page.size());parser.finish();}
  m.fetch={bf::State::Complete,bf::Error::None,200,4000};
  for(unsigned i=0;i<40;++i)step();assert(browser_app::snapshot().phase==Phase::Ready&&!m.engineStarts&&browser_app::snapshot().links==1);
  const int requests=m.fetchCalls;tap(70,15);step();browser_app::back();for(unsigned i=0;i<30;++i)step();assert(browser_app::snapshot().phase==Phase::Ready&&m.fetchCalls==requests);
  const unsigned beforeDragCalls=m.regionCalls;const unsigned beforeDragFrames=browser_app::snapshot().readerPaints;
  browser_app::touch(true,100,180);browser_app::touch(true,100,80);browser_app::touch(false,100,80);step();
  assert(browser_app::snapshot().scroll==200&&browser_app::snapshot().phase==Phase::Ready);
  assert(m.regionCalls-beforeDragCalls==22&&browser_app::snapshot().readerPaints==beforeDragFrames+1);
  // New samples coalesce to one complete frame and empty contacts do not redraw.
  const unsigned beforeCoalesce=m.regionCalls;
  browser_app::touch(true,100,180);browser_app::touch(true,100,160);browser_app::touch(true,100,140);step();
  assert(browser_app::snapshot().scroll==280&&m.regionCalls-beforeCoalesce==22);
  browser_app::touch(false,100,140);step();assert(m.regionCalls-beforeCoalesce==22);
  browser_app::touch(true,100,100);step();browser_app::touch(false,100,100);step();assert(m.regionCalls-beforeCoalesce==22);
  browser_app::touch(true,100,80);browser_app::touch(true,100,120);browser_app::touch(false,100,120);step();assert(browser_app::snapshot().scroll==200);
  browser_app::touch(true,100,80);browser_app::touch(true,100,180);browser_app::touch(false,100,180);for(unsigned i=0;i<30;++i)step();assert(browser_app::snapshot().scroll==0);
  tap(12,34);step();connectWifi();assert(m.request=="https://news.ycombinator.com/item?id=42"&&!browser_memory_live());
  cancelledWorker();step();tap(202,15);step();connectWifi();assert(!m.textMode&&!browser_app::snapshot().textMode);cancelledWorker();step();closeAndDestroy();assert(browser_memory_end()==canvas);

  // Closing or Stop cannot release the radio or object while a worker is alive.
  reset();startFetch("https://example.com/");const int stops=m.stopCalls;
  browser_app::close();browser_app::destroy();assert(browser_app::active());step();
  assert(m.stopCalls==stops && browser_app::finished()==browser_app::Destination::None);
  cancelledWorker();step();assert(browser_app::finished()==browser_app::Destination::Menu);
  diagnostic=browser_app::snapshot();assert(diagnostic.phase==Phase::Closed && diagnostic.fetchError==static_cast<unsigned>(bf::Error::Cancelled));
  browser_app::destroy();
  reset();startFetch("https://example.com/");tap(90,220);step();assert(m.worker && m.wifiActive);
  cancelledWorker();step();assert(browser_app::finished()==browser_app::Destination::None && !m.wifiActive);
  closeAndDestroy();

  // Replacing an in-flight request waits for its worker before starting a new join.
  reset();startFetch("https://example.com/old");const int oldJoins=m.joinCalls;
  editUrl("https://example.com/new");step();assert(m.worker && m.joinCalls==oldJoins);
  cancelledWorker();step();assert(m.joinCalls==oldJoins+1);connectWifi();
  assert(m.request=="https://example.com/new");finishWorker("https://example.com/new");step();ready();closeAndDestroy();

  // Four-entry history evicts the oldest URL and eventually returns to the menu.
  reset();startFetch("https://example.com/p0");finishWorker("https://example.com/p0");step();ready();
  for(int i=1;i<=5;++i){
    const std::string address="https://example.com/p"+std::to_string(i);
    m.href="/p"+std::to_string(i);tap(100,100);step();connectWifi();assert(m.request==address);
    finishWorker(address.c_str());step();ready();
  }
  for(int i=4;i>=1;--i){
    const std::string address="https://example.com/p"+std::to_string(i);
    browser_app::back();step();connectWifi();assert(m.request==address);
    finishWorker(address.c_str());step();ready();
  }
  browser_app::back();step();assert(browser_app::snapshot().page==2);browser_app::next();browser_app::activate();step();assert(browser_app::finished()==browser_app::Destination::Menu);browser_app::destroy();

  // A completed but unclaimed fetch is drained on close before destruction.
  reset();startFetch("https://example.com/");finishWorker("https://example.com/");browser_app::close();step();
  assert(m.releaseCount==1 && m.engineStarts==0 && browser_app::finished()==browser_app::Destination::Menu);browser_app::destroy();
  // No stored network hands control to network settings without opening a socket.
  reset();m.saved=false;assert(browser_app::open());step();
  assert(browser_app::finished()==browser_app::Destination::Wifi && m.fetchCalls==0 && m.joinCalls==0);browser_app::destroy();
  // A pending load may finish beneath the confirmation, then repaint on Stay.
  reset();startFetch("https://example.com/");browser_app::requestClose();step();
  assert(browser_app::snapshot().page==2 && m.worker);
  finishWorker("https://example.com/");step();assert(browser_app::snapshot().page==2 && m.engineActive);
  browser_app::back();step();ready();assert(browser_app::snapshot().page==0);closeAndDestroy();
  // Explicit Wi-Fi destination also drains asynchronous work.
  reset();startFetch("https://example.com/");tap(150,230);step();assert(browser_app::finished()==browser_app::Destination::None);
  assert(browser_app::snapshot().page==2);tap(175,142);
  cancelledWorker();step();assert(browser_app::finished()==browser_app::Destination::Wifi);browser_app::destroy();

  reset();m.joinOk=false;assert(browser_app::open());step();assert(!m.wifiActive && !m.fetchCalls);closeAndDestroy();
  reset();assert(browser_app::open());step();m.wifi.state=wn::State::Failed;step();assert(!m.wifiActive && !m.fetchCalls);closeAndDestroy();
  reset();m.fetchBeginOk=false;assert(browser_app::open());step();connectWifi();assert(!m.wifiActive && !m.worker);closeAndDestroy();
  // Failed download never starts the renderer and returns radio ownership.
  reset();startFetch("https://example.com/");m.tls=m.worker=false;m.fetch={bf::State::Failed,bf::Error::Tls,0,0};step();
  assert(!m.wifiActive && !m.engineStarts);
  diagnostic=browser_app::snapshot();assert(diagnostic.phase==Phase::Idle && !diagnostic.loading && !diagnostic.hasPage);
  assert(diagnostic.fetchError==static_cast<unsigned>(bf::Error::Tls) && diagnostic.fetchState==static_cast<unsigned>(bf::State::Failed));
  closeAndDestroy();
  // Respect the reserve and return fetched HTML when the renderer cannot start.
  reset();startFetch("https://example.com/");m.freeHeap=64*1024;finishWorker("https://example.com/");step();
  assert(m.engineStarts==0 && m.releaseCount==1 && !m.wifiActive);
  diagnostic=browser_app::snapshot();assert(diagnostic.engineError==BROWSER_ENGINE_ERROR_MEMORY && !diagnostic.enginePeak && !diagnostic.engineLive);
  assert(diagnostic.httpStatus==200 && diagnostic.received>0);closeAndDestroy();
  reset();startFetch("https://example.com/");m.freeHeap=100*1024;finishWorker("https://example.com/");step();ready();
  assert(m.budget==52*1024);closeAndDestroy();
  reset();startFetch("https://example.com/");m.startError=-1;finishWorker("https://example.com/");step();
  assert(m.engineStarts==1 && m.releaseCount==1);
  diagnostic=browser_app::snapshot();assert(diagnostic.engineError==BROWSER_ENGINE_ERROR_MEMORY && diagnostic.enginePeak==8192 && !diagnostic.engineLive);
  assert(m.engineError==BROWSER_ENGINE_ERROR_NONE);closeAndDestroy();
  reset();startFetch("https://example.com/");m.failRender=true;finishWorker("https://example.com/");step();
  assert(m.releaseCount==1 && !m.engineHtml && !m.wifiActive);
  diagnostic=browser_app::snapshot();assert(diagnostic.engineError==BROWSER_ENGINE_ERROR_LAYOUT && diagnostic.enginePeak==32768 && !diagnostic.engineLive);
  assert(m.engineError==BROWSER_ENGINE_ERROR_NONE);closeAndDestroy();
  reset();startFetch("https://example.com/");m.panelOk=false;finishWorker("https://example.com/");step();
  assert(m.releaseCount==1 && !m.engineHtml);
  assert(browser_app::snapshot().engineError==BROWSER_ENGINE_ERROR_PANEL);m.panelOk=true;closeAndDestroy();

  // An HTTP URL can redirect to HTTPS: Clock errors trigger a bounded clock retry.
  reset();fakeTime=0;startFetch("http://example.com/");
  m.tls=m.worker=false;m.fetch={bf::State::Failed,bf::Error::Clock,0,0};step();
  assert(m.ntp && m.wifiActive && !m.worker && m.fetchCalls==1);
  fakeTime=1735689600;step();assert(m.worker && !m.ntp && m.fetchCalls==2 && m.request=="http://example.com/");
  finishWorker("https://example.com/");step();ready();assert(!strcmp(browser_app::url(),"https://example.com/"));closeAndDestroy();
  reset();fakeTime=0;startFetch("http://example.com/");
  m.tls=m.worker=false;m.fetch={bf::State::Failed,bf::Error::Clock,0,0};step();assert(m.ntp && m.wifiActive);
  now+=20000;step();assert(!m.ntp && !m.wifiActive && m.fetchCalls==1);closeAndDestroy();
  // A second Clock error cannot create an unbounded fetch/sync loop.
  reset();fakeTime=0;startFetch("http://example.com/");
  m.tls=m.worker=false;m.fetch={bf::State::Failed,bf::Error::Clock,0,0};step();fakeTime=1735689600;step();
  assert(m.worker && m.fetchCalls==2);m.tls=m.worker=false;m.fetch={bf::State::Failed,bf::Error::Clock,0,0};step();
  assert(!m.ntp && !m.wifiActive && !m.worker && m.fetchCalls==2);closeAndDestroy();

  // Clock source and timeout are mocked: no host time, network, or RTC access.
  reset();fakeTime=0;m.rtcStatus=pet_rtc::Status::Valid;m.rtc=1735689600;
  assert(browser_app::open());step();connectWifi();assert(m.worker && !m.ntp && fakeTime==1735689600 && m.request=="https://news.ycombinator.com/");
  browser_app::close();cancelledWorker();step();browser_app::destroy();
  reset();fakeTime=0;assert(browser_app::open());step();connectWifi();assert(m.ntp && !m.worker);
  now+=20000;step();assert(!m.ntp && !m.wifiActive && !m.fetchCalls);closeAndDestroy();
  reset();fakeTime=0;assert(browser_app::open());step();connectWifi();assert(m.ntp && !m.worker);
  fakeTime=1735689600;step();assert(m.worker && !m.ntp);
  browser_app::close();cancelledWorker();step();browser_app::destroy();
  reset();fakeTime=0;assert(browser_app::open("http://example.com/"));step();connectWifi();assert(m.worker && !m.ntp);
  browser_app::close();cancelledWorker();step();browser_app::destroy();
  puts("Browser controller: URL entry, radio/TLS lifecycle, streaming rows, history, cancellation, failures, private diagnostics and mocked clock passed");
}
