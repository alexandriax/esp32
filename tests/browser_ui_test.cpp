#include "../firmware/sloth_pet/browser_ui.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
using namespace sloth;
static int key(char ch) {
  for(int i=0;i<BrowserUi::kKeyCount;++i)if(BrowserUi::keyCharacter(i)==ch)return i;
  assert(false);return -1;
}
static void type(BrowserUi& ui,const char* value) {
  for(;*value;++value)assert(ui.activate(key(*value)).type==BrowserEventType::Changed);
}
static void render(const BrowserUi& ui) {
  uint16_t row[242];
  for(unsigned y=0;y<240;++y) {
    for(unsigned i=0;i<242;++i)row[i]=0xabcd;
    drawBrowserRow(row+1,ui,y);
    assert(row[0]==0xabcd && row[241]==0xabcd);
  }
  for(unsigned i=0;i<242;++i)row[i]=0xabcd;
  drawBrowserRow(row+1,ui,240);drawBrowserRow(row+1,ui,UINT_MAX);drawBrowserRow(nullptr,ui,0);
  for(unsigned i=0;i<242;++i)assert(row[i]==0xabcd);
  for(int y=-1;y<=240;++y)for(int x=-1;x<=240;++x) {
    const int target=ui.hitTest(x,y);assert(target==-1 || ui.targetEnabled(target));
  }
}
int main() {
  static_assert(sizeof(BrowserUi)<1280,"UI must remain small; no framebuffer");
  BrowserUi ui;assert(!ui.isOpen());render(ui);assert(ui.activate().type==BrowserEventType::None);
  bool seen[127]={};
  for(int i=0;i<BrowserUi::kKeyCount;++i){const unsigned char ch=BrowserUi::keyCharacter(i);assert(ch>=32 && ch<=126 && !seen[ch]);seen[ch]=true;}
  for(int ch=32;ch<=126;++ch)assert(seen[ch]);
  assert(!BrowserUi::keyCharacter(-1) && !BrowserUi::keyCharacter(95));
  ui.show();assert(!strcmp(ui.url(),"https://news.ycombinator.com") && !ui.loading() && !ui.hasPage());render(ui);
  ui.show("");assert(!strcmp(ui.url(),"https://news.ycombinator.com"));
  assert(ui.hitTest(120,15)==BrowserUi::kUrl && ui.hitTest(40,220)==BrowserUi::kBack && ui.hitTest(0,0)==-1 && ui.hitTest(230,239)==-1);
  assert(ui.activate(BrowserUi::kUrl).type==BrowserEventType::Changed);
  assert(ui.page()==BrowserPage::Url && !strcmp(ui.draft(),ui.url()));render(ui);
  ui.activate(BrowserUi::kClear);assert(!*ui.draft() && !ui.targetEnabled(BrowserUi::kDone));
  ui.touch(true,20,65);assert(ui.touch(false,20,65).type==BrowserEventType::Changed);
  assert(!strcmp(ui.draft(),"a"));ui.activate(BrowserUi::kDelete);
  assert(ui.activate(BrowserUi::kDelete).type==BrowserEventType::None);
  type(ui,"https://Example.org/a?Case=One&x=%20#Here");
  assert(ui.activate(BrowserUi::kDone).type==BrowserEventType::Navigate);
  assert(!strcmp(ui.url(),"https://Example.org/a?Case=One&x=%20#Here") && !*ui.draft());
  // Cancel drops the draft without changing the committed URL.
  ui.activate(BrowserUi::kUrl);ui.activate(BrowserUi::kClear);type(ui,"discard me");
  assert(ui.back().type==BrowserEventType::Changed && !*ui.draft());
  assert(!strcmp(ui.url(),"https://Example.org/a?Case=One&x=%20#Here"));
  // A keyboard-only user can reach every enabled key and action.
  ui.activate(BrowserUi::kUrl);bool visited[99]={};
  for(int i=0;i<99;++i){visited[ui.focus()]=true;ui.next();assert(ui.scrollOffset()>=0 && ui.scrollOffset()<=ui.maxScroll());}
  for(int i=0;i<99;++i)assert(visited[i]==(i!=BrowserUi::kKeyboardBack));
  // One-row rendering remains safe throughout the scrollable keyboard.
  for(int i=0;i<100;++i){ui.next();render(ui);}
  ui.activate(BrowserUi::kClear);
  for(int i=0;i<BrowserUi::kUrlCapacity-1;++i)ui.activate(key('x'));
  assert(strlen(ui.draft())==BrowserUi::kUrlCapacity-1 && !ui.targetEnabled(key('x')));
  assert(ui.activate(key('y')).type==BrowserEventType::None);
  ui.activate(BrowserUi::kDelete);assert(strlen(ui.draft())==BrowserUi::kUrlCapacity-2);
  ui.activate(key('Z'));assert(ui.draft()[BrowserUi::kUrlCapacity-2]=='Z');render(ui);
  ui.activate(BrowserUi::kDone);assert(strlen(ui.url())==511);
  char longUrl[513];memset(longUrl,'q',512);longUrl[512]=0;
  assert(!ui.setUrl(longUrl) && strlen(ui.url())==511 && !strcmp(ui.status(),"ADDRESS TOO LONG"));
  assert(ui.setUrl("https://example.com"));
  // Keyboard drag clamps and never types the originally pressed key.
  ui.activate(BrowserUi::kUrl);char saved[512];strcpy(saved,ui.draft());
  ui.touch(true,20,90);ui.touch(true,20,-1000);assert(ui.scrollOffset()==ui.maxScroll());
  assert(ui.touch(false,20,90).type==BrowserEventType::None && !strcmp(saved,ui.draft()));render(ui);
  ui.touch(true,20,100);ui.touch(true,20,1000);assert(ui.scrollOffset()==0);ui.touch(false,20,100);ui.back();
  ui.setHasPage(true);ui.setCanGoBack(true);ui.setStatus("PAGE LOADED");render(ui);
  assert(ui.hasPage() && ui.canGoBack());
  // Stationary taps produce viewport-relative native coordinates.
  ui.touch(true,100,42);BrowserEvent event=ui.touch(false,100,42);
  assert(event.type==BrowserEventType::Link && event.x==200 && event.y==20);
  ui.touch(true,239,207);event=ui.touch(false,239,207);
  assert(event.type==BrowserEventType::Link && event.x==478 && event.y==350);
  ui.touch(true,100,150);event=ui.touch(true,100,120);
  assert(event.type==BrowserEventType::Scroll && event.delta==60);
  event=ui.touch(true,100,100);assert(event.type==BrowserEventType::Scroll && event.delta==40);
  assert(ui.touch(false,100,100).type==BrowserEventType::None);
  // A drag delivered only as press/release, or horizontal drag, cannot click links.
  ui.touch(true,100,100);assert(ui.touch(false,130,100).type==BrowserEventType::None);
  ui.touch(true,100,100);assert(ui.touch(false,100,150).type==BrowserEventType::None);
  ui.touch(true,100,100);ui.touch(true,INT_MIN,INT_MAX);assert(ui.touch(false,100,100).type==BrowserEventType::None);
  // Content changes/loading invalidate in-flight touches.
  ui.touch(true,100,100);ui.setLoading(true);assert(ui.touch(false,100,100).type==BrowserEventType::None);
  ui.touch(true,100,100);assert(ui.touch(false,100,100).type==BrowserEventType::None);
  assert(ui.activate(BrowserUi::kReload).type==BrowserEventType::Cancel);
  assert(!ui.targetEnabled(BrowserUi::kScrollDown));render(ui);
  ui.setLoading(false);assert(ui.activate(BrowserUi::kReload).type==BrowserEventType::Reload);
  assert(ui.activate(BrowserUi::kScrollUp).delta==-160 && ui.activate(BrowserUi::kScrollDown).delta==160);
  assert(ui.activate(BrowserUi::kBack).type==BrowserEventType::Back);
  assert(ui.activate(BrowserUi::kWifi).type==BrowserEventType::Wifi);
  assert(ui.activate(BrowserUi::kExit).type==BrowserEventType::RequestExit);
  ui.requestExit();assert(ui.page()==BrowserPage::ConfirmExit && ui.focus()==0);render(ui);
  ui.touch(true,60,142);assert(ui.touch(false,60,142).type==BrowserEventType::Changed && ui.page()==BrowserPage::Page);
  ui.requestExit();ui.next();assert(ui.focus()==1 && ui.activate().type==BrowserEventType::Exit);
  assert(ui.back().type==BrowserEventType::Changed && ui.page()==BrowserPage::Page);
  ui.setHasPage(false);ui.setLoading(true);ui.setUrl(longUrl+1);render(ui);ui.setLoading(false);
  ui.setHasPage(true);
  bool pageVisited[8]={};ui.activate(BrowserUi::kUrl);ui.back();
  for(int i=0;i<8;++i){pageVisited[ui.focus()]=true;ui.next();}
  for(bool value:pageVisited)assert(value);
  ui.setStatus("NETWORK FAILED - PLEASE OPEN WI-FI SETTINGS THEN TRY AGAIN");assert(strlen(ui.status())==39);render(ui);
  // Root can draw chrome only without touching any engine-owned page row.
  for(unsigned y=0;y<240;++y)assert(browserChromeRow(y)==(y<BROWSER_PAGE_TOP || y>=BROWSER_PAGE_BOTTOM));
  assert(!browserChromeRow(240));
  ui.close();assert(!ui.isOpen() && !*ui.url() && !*ui.draft() && !*ui.status());render(ui);
  puts("Browser UI: bounded ASCII editing, button fallback, row rendering, native coordinates, drag suppression and loading states passed");
}
