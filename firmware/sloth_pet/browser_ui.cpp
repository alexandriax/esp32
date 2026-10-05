#include "browser_ui.h"
#include "pet_canvas.h"
#include <stdio.h>
#include <string.h>

namespace sloth {
namespace {
void copy(char* dst,size_t capacity,const char* src) {
  size_t n=0;if(src)while(n+1<capacity && src[n]){dst[n]=src[n];++n;}dst[n]=0;
}
bool fits(const char* text,size_t capacity) {
  if(!text)return true;
  for(size_t n=0;n<capacity;++n)if(!text[n])return true;
  return false;
}
int bounded(int value) { return value< -240?-240:value>480?480:value; }
using namespace graphics;

// Row-local drawing shares the normal UI's palette and uppercase/numeric font.
// Supplementary glyphs preserve case and punctuation in URLs and the keyboard.
struct Row {
  uint16_t* pixels;int at;
  void rect(int x,int y,int width,int height,uint16_t color) {
    if(at<y || at>=y+height)return;
    const int end=x+width<240?x+width:240;
    for(int xx=x<0?0:x;xx<end;++xx)pixels[xx]=color;
  }
  void glyph(int x,int y,char ch,uint16_t color,int scale=1) { drawKeyboardGlyphRow(pixels,at,x,y,ch,color,scale); }
  void text(int x,int y,const char* value,uint16_t color,size_t limit=40,int scale=1) {
    if(!value || at<y || at>=y+7*scale)return;
    for(size_t i=0;i<limit && value[i];++i)glyph(x+static_cast<int>(i)*6*scale,y,value[i],color,scale);
  }
  void centered(int y,const char* value,uint16_t color,int scale=1) {
    const int count=static_cast<int>(strlen(value));text((240-(count*6-1)*scale)/2,y,value,color,40,scale);
  }
  void button(int x,int y,int width,int height,const char* label,bool focus,bool enabled=true) {
    rect(x,y,width,height,focus && enabled?gold:rgb(30,57,48));
    if(focus && enabled)rect(x+1,y+1,width-2,height-2,rgb(30,57,48));
    text(x+(width-(static_cast<int>(strlen(label))*6-1))/2,y+(height-7)/2,label,enabled?cream:muted);
  }
};
const char* focusLabel(int focus,bool loading) {
  switch(focus) {
    case BrowserUi::kScrollUp:return "SELECT: SCROLL UP";
    case BrowserUi::kScrollDown:return "SELECT: SCROLL DOWN";
    case BrowserUi::kBack:return "SELECT: BACK";
    case BrowserUi::kReload:return loading?"SELECT: STOP":"SELECT: RELOAD";
    case BrowserUi::kWifi:return "SELECT: WI-FI";
    case BrowserUi::kExit:return "SELECT: EXIT";
    default:return nullptr;
  }
}
} // namespace

KeyboardSpec BrowserUi::keyboardSpec() const {
  KeyboardSpec s;s.text=draft_;s.keys=Keyboard::asciiKeys();s.title="WEB ADDRESS";
  s.capacity=sizeof(draft_);s.doneEnabled=draft_[0];s.labels[2]="CANCEL";s.labels[3]="GO";return s;
}
BrowserEvent BrowserUi::keyboardEvent(KeyboardEvent event){
  if(event==KeyboardEvent::Back || event==KeyboardEvent::Cancel)return back();
  if(event==KeyboardEvent::Done){copy(url_,sizeof(url_),draft_);draft_[0]=0;page_=BrowserPage::Page;focus_=kUrl;scroll_=0;return BrowserEvent(BrowserEventType::Navigate);}
  return BrowserEvent(event==KeyboardEvent::Changed?BrowserEventType::Changed:BrowserEventType::None);
}
void BrowserUi::show(const char* url) {
  close();textMode_=true;open_=true;focus_=kUrl;setUrl(url && *url?url:"https://news.ycombinator.com");
  if(!*status_)setStatus("ENTER A WEB ADDRESS");
}
void BrowserUi::requestExit() {
  if(!open_ || page_==BrowserPage::ConfirmExit)return;
  returnPage_=page_;page_=BrowserPage::ConfirmExit;focus_=0;cancelTouch();
}
void BrowserUi::close() {
  open_=loading_=hasPage_=canGoBack_=false;page_=BrowserPage::Page;focus_=kUrl;scroll_=0;
  url_[0]=draft_[0]=status_[0]=0;cancelTouch();
}
bool BrowserUi::setUrl(const char* url) {
  if(!fits(url,sizeof(url_))){setStatus("ADDRESS TOO LONG");return false;}
  copy(url_,sizeof(url_),url);cancelTouch();return true;
}
void BrowserUi::setStatus(const char* status) { copy(status_,sizeof(status_),status); }
void BrowserUi::setLoading(bool loading) { if(loading_!=loading){loading_=loading;cancelTouch();if(page_==BrowserPage::Page && !targetEnabled(focus_))focus_=kReload;} }
void BrowserUi::setHasPage(bool available) { if(hasPage_!=available){hasPage_=available;cancelTouch();if(page_==BrowserPage::Page && !targetEnabled(focus_))focus_=kUrl;} }
int BrowserUi::maxScroll() const { return page_==BrowserPage::Url?Keyboard::maxScroll(keyboardSpec()):0; }
char BrowserUi::keyCharacter(int target) { return target>=0 && target<kKeyCount?Keyboard::asciiKeys()[target]:0; }
bool BrowserUi::targetEnabled(int target) const {
  if(!open_)return false;
  if(target==kNavBack || target==kNavNext)return true;
  if(target==kNavSelect)return targetEnabled(focus());
  if(page_==BrowserPage::Url)return target==kNavSelect?Keyboard::enabled(keyboardSpec(),keyboard_.focus()):Keyboard::enabled(keyboardSpec(),target);
  if(page_==BrowserPage::ConfirmExit)return target==0 || target==1;
  if(target<kUrl || target>kMode)return false;
  if(target==kScrollUp || target==kScrollDown)return hasPage_ && !loading_;
  if(target==kReload)return loading_ || url_[0];
  return true;
}
void BrowserUi::cancelTouch() { keyboard_.cancelTouch(); touching_=dragged_=scrollGesture_=false;touchTarget_=-1; }
void BrowserUi::next() {
  if(!open_)return;
  if(page_==BrowserPage::Url){keyboard_.next(keyboardSpec());return;}
  cancelTouch();const int count=page_==BrowserPage::ConfirmExit?2:kMode+1;
  for(int i=0;i<count;++i){focus_=(focus_+1)%count;if(targetEnabled(focus_))break;}

}
BrowserEvent BrowserUi::activate(int target) {
  if(!open_)return BrowserEvent();
  if(page_==BrowserPage::Url)return keyboardEvent(keyboard_.activate(keyboardSpec(),draft_,target));
  if(target==kNavBack)return back();
  if(target==kNavNext){next();return BrowserEvent(BrowserEventType::Changed);}
  if(target<0 || target==kNavSelect)target=focus_;
  if(!targetEnabled(target))return BrowserEvent();
  cancelTouch();focus_=target;

  if(page_==BrowserPage::ConfirmExit){
    if(target==0)return back();
    return BrowserEvent(BrowserEventType::Exit);
  }
  switch(target) {
    case kMode:textMode_=!textMode_;return BrowserEvent(BrowserEventType::Mode);
    case kUrl:copy(draft_,sizeof(draft_),url_);page_=BrowserPage::Url;focus_=0;scroll_=0;keyboard_.reset();return BrowserEvent(BrowserEventType::Changed);
    case kBack:return BrowserEvent(BrowserEventType::Back);
    case kReload:return BrowserEvent(loading_?BrowserEventType::Cancel:BrowserEventType::Reload);
    case kWifi:return BrowserEvent(BrowserEventType::Wifi);
    case kExit:return BrowserEvent(BrowserEventType::RequestExit);
    case kScrollUp:return BrowserEvent(BrowserEventType::Scroll,-160);
    case kScrollDown:return BrowserEvent(BrowserEventType::Scroll,160);
    default:return BrowserEvent();
  }
}
BrowserEvent BrowserUi::back() {
  if(!open_)return BrowserEvent();
  cancelTouch();if(page_==BrowserPage::ConfirmExit){page_=returnPage_;focus_=kUrl;return BrowserEvent(BrowserEventType::Changed);}
  if(page_==BrowserPage::Url){draft_[0]=0;page_=BrowserPage::Page;focus_=kUrl;scroll_=0;return BrowserEvent(BrowserEventType::Changed);}
  return BrowserEvent(BrowserEventType::Back);
}
int BrowserUi::hitTest(int x,int y) const {
  if(!open_ || x<0 || y<0 || x>=240 || y>=240)return -1;
  if(page_==BrowserPage::Url)return keyboard_.hitTest(keyboardSpec(),x,y);
  if(page_==BrowserPage::ConfirmExit)return y>=125 && y<162?(x>=12 && x<114?0:x>=126 && x<228?1:-1):-1;
  if(y>=8&&y<23&&x>=18&&x<222)return x>=182?kMode:kUrl;
  if(y>=208&&y<232&&x>=16&&x<224){const int target=kBack+(x-16)/52;return targetEnabled(target)?target:-1;}
  return -1;
}
BrowserEvent BrowserUi::touch(bool down,int x,int y) {
  if(!open_)return BrowserEvent();
  if(page_==BrowserPage::Url)return keyboardEvent(keyboard_.touch(keyboardSpec(),draft_,down,x,y));
  if(down && !touching_) {
    if(x<0 || y<0 || x>=240 || y>=240)return BrowserEvent();
    touching_=true;startX_=x;startY_=lastY_=y;startScroll_=scroll_;touchTarget_=hitTest(x,y);
    scrollGesture_=page_==BrowserPage::Page && hasPage_ && !loading_ && y>=static_cast<int>(BROWSER_PAGE_TOP) && y<static_cast<int>(BROWSER_PAGE_BOTTOM);
    return BrowserEvent();
  }
  if(!touching_)return BrowserEvent();
  const int dx=bounded(x)-startX_,dy=bounded(y)-startY_;
  if(dx>7 || dx< -7 || dy>7 || dy< -7)dragged_=true;
  if(down) {
    if(dragged_ && scrollGesture_) {
      const int delta=(lastY_-bounded(y))*2;lastY_=bounded(y);
      return delta?BrowserEvent(BrowserEventType::Scroll,delta):BrowserEvent();
    }
    return BrowserEvent();
  }
  const bool tap=!dragged_ && x>=0 && x<240 && y>=0 && y<240;
  const int target=tap && touchTarget_==hitTest(x,y)?touchTarget_:-1;
  const bool link=tap && scrollGesture_ && page_==BrowserPage::Page && y>=static_cast<int>(BROWSER_PAGE_TOP) && y<static_cast<int>(BROWSER_PAGE_BOTTOM);
  cancelTouch();
  if(target>=0)return activate(target);
  return link?BrowserEvent(BrowserEventType::Link,0,x*2,(y-static_cast<int>(BROWSER_PAGE_TOP))*2):BrowserEvent();
}

void drawBrowserRow(uint16_t* pixels,const BrowserUi& ui,unsigned y) {
  if(!pixels || y>=240)return;
  Row row{pixels,static_cast<int>(y)};row.rect(0,0,240,240,ink);if(!ui.isOpen())return;
  if(ui.page()==BrowserPage::Url){drawKeyboardRow(pixels,ui.keyboardSpec(),ui.keyboard(),y);return;}
  if(ui.page()==BrowserPage::ConfirmExit){
    row.centered(63,"LEAVE BROWSER?",cream,2);
    row.centered(92,"BACK KEEPS THIS PAGE OPEN",muted);
    row.button(12,125,102,37,"STAY",ui.focus()==0);
    row.button(126,125,102,37,"EXIT",ui.focus()==1);
    return;
  }
  row.rect(0,0,240,BROWSER_PAGE_TOP,rgb(30,57,48));
  if(ui.focus()==BrowserUi::kUrl)row.rect(18,8,160,1,gold);
  const char* focused=focusLabel(ui.focus(),ui.loading());
  if(ui.focus()==BrowserUi::kScrollUp || ui.focus()==BrowserUi::kScrollDown)row.text(18,13,focused,gold,26);
  else row.text(18,13,ui.url(),cream,26);
  row.button(182,8,40,15,ui.textMode()?"READ":"HTML",ui.focus()==BrowserUi::kMode);
  row.text(12,24,ui.status(),ui.loading()?gold:muted,36);
  if(ui.loading() && !ui.hasPage()) {
    row.centered(55,"LOADING PAGE",cream,2);
    row.centered(87,ui.status(),gold);
    const size_t length=strlen(ui.url());
    for(size_t line=0;line<3 && line*38<length;++line){
      char part[39]{};size_t n=length-line*38;if(n>38)n=38;
      memcpy(part,ui.url()+line*38,n);
      if(line==2 && length>114)memcpy(part+35,"...",3);
      row.text(6,112+static_cast<int>(line)*14,part,cream,38);
    }
    row.centered(177,"STOP CANCELS THIS LOAD",muted);
  }else if(!ui.hasPage()) {
    row.centered(47,"BROWSER",cream,2);
    row.centered(75,"DIRECT WI-FI BROWSING",mint);
    row.centered(101,ui.status(),gold);
    row.centered(137,"TAP THE URL BAR TO ENTER",cream);
    row.centered(149,"AN HTTP OR HTTPS ADDRESS",cream);
    row.centered(176,"NEXT / SELECT ALSO WORK",muted);
    row.centered(190,"WI-FI SETTINGS BELOW",muted);
  }
  const char* labels[]={"BACK",ui.loading()?"STOP":"RELOAD","WI-FI","EXIT"};
  for(int i=0;i<4;++i)row.button(16+i*52,208,52,24,labels[i],ui.focus()==BrowserUi::kBack+i,ui.targetEnabled(BrowserUi::kBack+i));
}
} // namespace sloth
