#include "../firmware/sloth_pet/keyboard.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
using namespace sloth;
namespace {
KeyboardEvent tap(Keyboard& k,KeyboardSpec& s,char* text,int x,int y){
  assert(k.touch(s,text,true,x,y)==KeyboardEvent::None);
  return k.touch(s,text,false,x,y);
}
void keysAndBounds(){
  char text[97]={};KeyboardSpec s;s.text=text;s.capacity=sizeof(text);s.keys=Keyboard::asciiKeys();Keyboard k;
  assert(strlen(s.keys)==95 && Keyboard::maxScroll(s)==330);
  bool seen[128]={};
  for(int key=0;key<95;++key){
    while(k.focus()!=key)k.next(s);
    const int x=8+(key%6)*38,y=54+(key/6)*30-k.scroll();
    assert(y>=54 && y+28<=202);
    for(int yy=y;yy<y+28;++yy)for(int xx=x;xx<x+34;++xx)assert(k.hitTest(s,xx,yy)==key);
    assert(tap(k,s,text,x+26,y+20)==KeyboardEvent::Changed);
    assert(text[key]==s.keys[key] && !seen[static_cast<unsigned char>(text[key])]);
    seen[static_cast<unsigned char>(text[key])]=true;
  }
  for(int ch=32;ch<=126;++ch)assert(seen[ch]);
  assert(strlen(text)==95);
  assert(k.activate(s,text,0)==KeyboardEvent::Changed && strlen(text)==96);
  assert(k.activate(s,text,1)==KeyboardEvent::None && strlen(text)==96);
  k.next(s);assert(k.focus()==95); // Full buffer skips character keys.
  assert(tap(k,s,text,44,218)==KeyboardEvent::Changed && strlen(text)==95);
  assert(tap(k,s,text,120,218)==KeyboardEvent::Changed && !*text);
  assert(k.hitTest(s,44,218)==-1 && k.hitTest(s,120,218)==-1);
  assert(!Keyboard::enabled(s,s.actions[2])); // Back is in the navigation rail, not a duplicate footer.
  k.reset(96);k.next(s);assert(k.focus()==98);
  unsigned visible=0;for(int row=0;row<5;++row)for(int col=0;col<6;++col)visible+=k.hitTest(s,25+col*38,68+row*30)>=0;
  assert(visible==30);
  s.doneEnabled=false;assert(k.hitTest(s,196,218)==-1);
  s.doneEnabled=true;assert(tap(k,s,text,196,218)==KeyboardEvent::Done);
  assert(tap(k,s,text,120,15)==KeyboardEvent::Back);
}
void gestures(){
  char text[12]={};KeyboardSpec s;s.text=text;s.capacity=sizeof(text);s.keys=Keyboard::asciiKeys();Keyboard k;
  for(int i=0;i<100;++i)k.touch(s,text,true,40,68);
  assert(!*text);k.touch(s,text,false,40,68);assert(!strcmp(text,"a"));
  k.touch(s,text,false,40,68);assert(!strcmp(text,"a"));
  k.touch(s,text,true,40,68);k.touch(s,text,true,80,100);k.touch(s,text,true,40,68);k.touch(s,text,false,40,68);
  assert(!strcmp(text,"a"));
  k.touch(s,text,true,120,150);k.touch(s,text,true,120,-2000);k.touch(s,text,false,120,-2000);
  assert(k.scroll()==330 && !strcmp(text,"a"));
  k.touch(s,text,true,44,218);k.touch(s,text,true,60,68);k.touch(s,text,false,60,68);
  assert(k.scroll()==330 && !strcmp(text,"a")); // Footer cannot start scrolling.
  k.touch(s,text,true,120,100);k.touch(s,text,false,INT_MAX,INT_MAX);assert(k.scroll()==0);
  k.touch(s,text,true,40,68);k.cancelTouch();k.touch(s,text,true,40,68);k.touch(s,text,false,40,68);
  assert(!strcmp(text,"a"));assert(tap(k,s,text,40,68)==KeyboardEvent::Changed);
  k.touch(s,text,true,40,68);k.next(s);k.touch(s,text,false,40,68);assert(!strcmp(text,"aa"));
  assert(tap(k,s,text,60,68)==KeyboardEvent::Changed && !strcmp(text,"aab"));
  k.touch(s,text,true,INT_MIN,INT_MAX);k.touch(s,text,false,INT_MIN,INT_MAX);assert(!strcmp(text,"aab"));
}
void profilesAndRendering(){
  char text[12]={};const uint8_t targets[]={0,26};KeyboardSpec s;s.text=text;s.capacity=sizeof(text);
  s.keys="A ";s.targets=targets;s.cleanSpaces=true;s.actions[0]=27;s.actions[1]=30;s.actions[2]=29;s.actions[3]=28;s.targetCount=31;s.modeAction=true;
  Keyboard k;assert(!Keyboard::enabled(s,26));
  assert(k.activate(s,text,30)==KeyboardEvent::Mode && !*text);
  k.activate(s,text,0);k.activate(s,text,26);assert(!strcmp(text,"A "));
  assert(k.activate(s,text,26)==KeyboardEvent::None);k.activate(s,text,27);assert(!strcmp(text,"A"));
  // Copying UI state cannot leave a pointer into the original caller's draft.
  Keyboard copied=k;char other[12]="B";KeyboardSpec otherSpec=s;otherSpec.text=other;
  copied.activate(otherSpec,other,0);assert(!strcmp(other,"BA") && !strcmp(text,"A"));
  static uint16_t a[240*240+2],b[240*240+2];a[0]=a[240*240+1]=b[0]=b[240*240+1]=0xbeef;
  s=KeyboardSpec();s.text="Secr3t!";s.capacity=64;s.keys=Keyboard::asciiKeys();s.masked=true;k.reset();
  drawKeyboard(a+1,s,k);s.text="Diff4r?";
  for(unsigned y=0;y<240;++y)drawKeyboardRow(b+1+y*240,s,k,y);
  assert(!memcmp(a,b,sizeof(a))); // Masked contents and row/full rendering are identical.
  assert(a[0]==0xbeef && a[240*240+1]==0xbeef);
  s.masked=false;drawKeyboard(b+1,s,k);assert(memcmp(a,b,sizeof(a)));
  drawKeyboard(nullptr,s,k);drawKeyboardRow(nullptr,s,k,0);drawKeyboardRow(b+1,s,k,240);
}
}
int main(){keysAndBounds();gestures();profilesAndRendering();puts("Shared keyboard: all printable ASCII, large touch targets, bounded edits, profiles, hardware, gestures, masking and row rendering passed");}
