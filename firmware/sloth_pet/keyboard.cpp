#include "keyboard.h"
#include "pet_canvas.h"
#include <string.h>
#include <stdio.h>
namespace sloth {
namespace {
using namespace graphics;
const uint8_t lower[26][5]={
  {0x20,0x54,0x54,0x54,0x78},{0x7f,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7f},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7e,0x09,0x01,0x02},
  {0x0c,0x52,0x52,0x52,0x3e},{0x7f,0x08,0x04,0x04,0x78},{0,0x44,0x7d,0x40,0},
  {0x20,0x40,0x44,0x3d,0},{0x7f,0x10,0x28,0x44,0},{0,0x41,0x7f,0x40,0},
  {0x7c,0x04,0x78,0x04,0x78},{0x7c,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7c,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7c},{0x7c,0x08,0x04,0x04,0x08},
  {0x48,0x54,0x54,0x54,0x20},{0x04,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},
  {0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},{0x44,0x28,0x10,0x28,0x44},
  {0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44}
};
struct Row {
  uint16_t* pixels;int at;
  void rect(int x,int y,int width,int height,uint16_t color) {
    if(at<y || at>=y+height)return;
    const int end=x+width<240?x+width:240;
    for(int xx=x<0?0:x;xx<end;++xx)pixels[xx]=color;
  }
  void glyph(int x,int y,char ch,uint16_t color,int scale=1) {
    if(at<y || at>=y+7*scale)return;
    uint8_t extra[5]={};const uint8_t* bits=extra;
    if(ch>='A' && ch<='Z')bits=letters[ch-'A'];
    else if(ch>='a' && ch<='z')bits=lower[ch-'a'];
    else if(ch>='0' && ch<='9')bits=numbers[ch-'0'];
    else switch(ch) {
      case ' ':break;
      case '!':extra[2]=0x5f;break;
      case '"':extra[1]=extra[3]=3;break;
      case '#':extra[0]=extra[2]=extra[4]=0x14;extra[1]=extra[3]=0x7f;break;
      case '$':extra[0]=0x24;extra[1]=0x2a;extra[2]=0x7f;extra[3]=0x2a;extra[4]=0x12;break;
      case '%':extra[0]=0x63;extra[1]=0x13;extra[2]=8;extra[3]=0x64;extra[4]=0x63;break;
      case '&':extra[0]=0x36;extra[1]=0x49;extra[2]=0x55;extra[3]=0x22;extra[4]=0x50;break;
      case '\'':extra[2]=3;break;
      case '(':extra[2]=0x3e;extra[3]=0x41;break;
      case ')':extra[1]=0x41;extra[2]=0x3e;break;
      case '*':extra[0]=extra[4]=0x14;extra[1]=extra[3]=8;extra[2]=0x3e;break;
      case '+':extra[1]=extra[3]=8;extra[2]=0x3e;break;
      case ',':extra[1]=0x40;extra[2]=0x20;break;
      case '-':extra[1]=extra[2]=extra[3]=8;break;
      case '.':extra[2]=0x60;break;
      case '/':extra[0]=0x40;extra[1]=0x20;extra[2]=0x18;extra[3]=4;extra[4]=2;break;
      case ':':extra[2]=0x36;break;
      case ';':extra[1]=0x40;extra[2]=0x36;break;
      case '<':extra[1]=8;extra[2]=0x14;extra[3]=0x22;break;
      case '=':extra[0]=extra[1]=extra[2]=extra[3]=extra[4]=0x14;break;
      case '>':extra[1]=0x22;extra[2]=0x14;extra[3]=8;break;
      case '@':extra[0]=0x3e;extra[1]=0x41;extra[2]=0x5d;extra[3]=0x55;extra[4]=0x1e;break;
      case '[':extra[1]=0x7f;extra[2]=extra[3]=0x41;break;
      case '\\':extra[0]=2;extra[1]=4;extra[2]=8;extra[3]=0x10;extra[4]=0x20;break;
      case ']':extra[3]=0x7f;extra[1]=extra[2]=0x41;break;
      case '^':extra[0]=extra[4]=4;extra[1]=extra[3]=2;extra[2]=1;break;
      case '_':extra[0]=extra[1]=extra[2]=extra[3]=extra[4]=0x40;break;
      case '`':extra[1]=1;extra[2]=2;break;
      case '{':extra[1]=8;extra[2]=0x36;extra[3]=0x41;break;
      case '|':extra[2]=0x7f;break;
      case '}':extra[1]=0x41;extra[2]=0x36;extra[3]=8;break;
      case '~':extra[0]=8;extra[1]=4;extra[2]=8;extra[3]=0x10;extra[4]=8;break;
      default:extra[0]=2;extra[1]=1;extra[2]=0x51;extra[3]=9;extra[4]=6;break;
    }
    const unsigned bit=1u<<((at-y)/scale);
    for(int col=0;col<5;++col)if(bits[col]&bit)rect(x+col*scale,at,scale,1,color);
  }
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

int footerCount(const KeyboardSpec& s){return s.showCancel?4:3;}
int footerWidth(const KeyboardSpec& s){return s.showCancel?53:72;}
int footerIndex(const KeyboardSpec& s,int column){return !s.showCancel && column==2?3:column;}
int keyTarget(const KeyboardSpec& s,int index){return s.targets?s.targets[index]:index;}
bool inside(int x,int y,int left,int top,int width,int height){return x>=left && x<left+width && y>=top && y<top+height;}
}
const char* Keyboard::asciiKeys(){return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";}
char Keyboard::character(const KeyboardSpec& s,int target){
  if(s.keys)for(int i=0;s.keys[i];++i)if(keyTarget(s,i)==target)return s.keys[i];
  return 0;
}
int Keyboard::maxScroll(const KeyboardSpec& s){
  const int count=s.keys?static_cast<int>(strlen(s.keys)):0;
  const int content=((count+kColumns-1)/kColumns)*kPitchY-(kPitchY-kHeight);
  return content>kBottom-kTop?content-(kBottom-kTop):0;
}
bool Keyboard::enabled(const KeyboardSpec& s,int target){
  if(target==s.navNext || target==s.navBack)return true;
  if(target<0 || target>=s.targetCount)return false;
  const size_t length=strlen(s.text);
  const char ch=character(s,target);
  if(ch)return length+1<s.capacity && !(ch==' ' && s.cleanSpaces && (!length || s.text[length-1]==' '));
  if(target==s.actions[0])return length!=0;
  if(target==s.actions[1])return s.modeAction || length!=0;
  if(target==s.actions[2])return s.showCancel;
  if(target==s.actions[3])return s.doneEnabled;
  return false;
}
void Keyboard::reset(int focus){cancelTouch();focus_=focus;scroll_=0;}
void Keyboard::cancelTouch(){if(touching_)releaseRequired_=true;touching_=dragged_=scrolling_=false;target_=-1;}
void Keyboard::reveal(const KeyboardSpec& s){
  if(!s.keys)return;
  for(int i=0;s.keys[i];++i)if(keyTarget(s,i)==focus_){
    const int top=(i/kColumns)*kPitchY;
    if(top<scroll_)scroll_=top;
    if(top+kHeight>scroll_+kBottom-kTop)scroll_=top+kHeight-(kBottom-kTop);
    if(scroll_>maxScroll(s))scroll_=maxScroll(s);
    return;
  }
}
void Keyboard::next(const KeyboardSpec& s){
  cancelTouch();
  for(int i=0;i<s.targetCount;++i){focus_=(focus_+1)%s.targetCount;if(enabled(s,focus_))break;}
  reveal(s);
}
KeyboardEvent Keyboard::activate(const KeyboardSpec& s,char* text,int target){
  if(target==s.navNext){next(s);return KeyboardEvent::Changed;}
  if(target==s.navBack)return KeyboardEvent::Back;
  if(target==-1 || target==s.navSelect)target=focus_;
  if(!enabled(s,target))return KeyboardEvent::None;
  cancelTouch();focus_=target;reveal(s);
  const size_t length=strlen(text);const char ch=character(s,target);
  if(ch){text[length]=ch;text[length+1]=0;return KeyboardEvent::Changed;}
  if(target==s.actions[0]){text[length-1]=0;return KeyboardEvent::Changed;}
  if(target==s.actions[1]){if(s.modeAction)return KeyboardEvent::Mode;text[0]=0;return KeyboardEvent::Changed;}
  if(target==s.actions[2])return KeyboardEvent::Cancel;
  if(target==s.actions[3])return KeyboardEvent::Done;
  return KeyboardEvent::None;
}
int Keyboard::hitTest(const KeyboardSpec& s,int x,int y) const {
  if(x<0 || y<0 || x>=240 || y>=240)return -1;
  if(inside(x,y,20,8,200,16))return x<86?s.navNext:x<154?s.navBack:enabled(s,focus_)?s.navSelect:-1;
  if(y>=kTop && y<kBottom && s.keys)for(int i=0;s.keys[i];++i){
    const int top=kTop+(i/kColumns)*kPitchY-scroll_,target=keyTarget(s,i);
    if(top>=kTop && top+kHeight<=kBottom && inside(x,y,kLeft+(i%kColumns)*kPitchX,top,kWidth,kHeight))return enabled(s,target)?target:-1;
  }
  for(int i=0;i<footerCount(s);++i){const int target=s.actions[footerIndex(s,i)],width=footerWidth(s);if(inside(x,y,8+i*(width+4),204,width,28))return enabled(s,target)?target:-1;}
  return -1;
}
KeyboardEvent Keyboard::touch(const KeyboardSpec& s,char* text,bool down,int x,int y){
  if(releaseRequired_){if(!down)releaseRequired_=false;return KeyboardEvent::None;}
  if(down && !touching_){
    if(x<0 || y<0 || x>=240 || y>=240)return KeyboardEvent::None;
    touching_=true;startX_=x;startY_=y;startScroll_=scroll_;target_=hitTest(s,x,y);
    scrolling_=y>=kTop && y<kBottom;return KeyboardEvent::None;
  }
  if(!touching_)return KeyboardEvent::None;
  const int64_t dx=static_cast<int64_t>(x)-startX_,dy=static_cast<int64_t>(y)-startY_;
  if(dx>7 || dx< -7 || dy>7 || dy< -7)dragged_=true;
  if(dragged_ && scrolling_){const int64_t offset=static_cast<int64_t>(startScroll_)-dy;scroll_=offset<0?0:offset>maxScroll(s)?maxScroll(s):static_cast<int>(offset);}
  if(down)return dragged_ && scrolling_?KeyboardEvent::Changed:KeyboardEvent::None;
  const int target=!dragged_ && target_==hitTest(s,x,y)?target_:-1;
  touching_=dragged_=scrolling_=false;target_=-1;
  return target>=0?activate(s,text,target):KeyboardEvent::None;
}
void drawKeyboardRow(uint16_t* pixels,const KeyboardSpec& s,const Keyboard& keyboard,unsigned y){
  if(!pixels || y>=240)return;
  Row row{pixels,static_cast<int>(y)};row.rect(0,0,240,240,ink);
  row.text(30,12,"NEXT",muted);row.text(98,12,"BACK",muted);row.text(164,12,"SELECT",muted);
  row.rect(8,28,224,22,rgb(30,57,48));
  const size_t n=strlen(s.text),offset=n>27?n-27:0;
  if(n){for(size_t i=offset;i<n;++i)row.glyph(13+static_cast<int>(i-offset)*6,36,s.masked?'*':s.text[i],cream);}
  else row.text(13,36,*s.placeholder?s.placeholder:s.title,muted,27);
  char count[24];snprintf(count,sizeof(count),"%u/%u",static_cast<unsigned>(n),static_cast<unsigned>(s.capacity-1));
  row.text(184,36,count,muted,7);
  if(s.keys)for(int i=0;s.keys[i];++i){
    const int top=Keyboard::kTop+(i/Keyboard::kColumns)*Keyboard::kPitchY-keyboard.scroll();
    if(top<Keyboard::kTop || top+Keyboard::kHeight>Keyboard::kBottom)continue;
    const int x=Keyboard::kLeft+(i%Keyboard::kColumns)*Keyboard::kPitchX,target=keyTarget(s,i);
    const bool enabled=Keyboard::enabled(s,target),focused=keyboard.focus()==target;
    row.button(x,top,Keyboard::kWidth,Keyboard::kHeight,"",focused,enabled);
    if(s.keys[i]==' ')row.text(x+(Keyboard::kWidth-29)/2,top+(Keyboard::kHeight-7)/2,"SPACE",enabled?cream:muted);
    else row.glyph(x+(Keyboard::kWidth-10)/2,top+(Keyboard::kHeight-14)/2,s.keys[i],enabled?cream:muted,2);
  }
  const int maximum=Keyboard::maxScroll(s);
  if(maximum){row.rect(235,Keyboard::kTop,2,Keyboard::kBottom-Keyboard::kTop,muted);row.rect(235,Keyboard::kTop+keyboard.scroll()*(Keyboard::kBottom-Keyboard::kTop-12)/maximum,2,12,gold);}
  for(int i=0;i<footerCount(s);++i){const int index=footerIndex(s,i),width=footerWidth(s);row.button(8+i*(width+4),204,width,28,s.labels[index],keyboard.focus()==s.actions[index],Keyboard::enabled(s,s.actions[index]));}
}
void drawKeyboard(uint16_t* pixels,const KeyboardSpec& s,const Keyboard& keyboard){if(pixels)for(unsigned y=0;y<240;++y)drawKeyboardRow(pixels+y*240,s,keyboard,y);}
void drawKeyboardGlyphRow(uint16_t* pixels,int at,int x,int y,char ch,uint16_t color,int scale){if(pixels && at>=0 && at<240){Row row{pixels,at};row.glyph(x,y,ch,color,scale);}}
void drawKeyboardText(uint16_t* pixels,int x,int y,const char* text,uint16_t color,size_t limit){
  if(!pixels || !text)return;
  for(int yy=y<0?0:y;yy<y+7 && yy<240;++yy){Row row{pixels+yy*240,yy};row.text(x,y,text,color,limit);}
}
}
