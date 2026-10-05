#pragma once
#include <stddef.h>
#include <stdint.h>
namespace sloth {
// Caller owns the bounded text and commits/cancels it. This view is never retained.
struct KeyboardSpec {
  const char* text=""; const char* keys=nullptr; const uint8_t* targets=nullptr;
  const char* title="ENTER TEXT"; const char* placeholder="";
  const char* labels[4]={"DELETE","CLEAR","BACK","DONE"};
  int actions[4]={95,96,97,98}; int targetCount=99;
  int navNext=101,navBack=100,navSelect=102;
  size_t capacity=1;
  bool masked=false,doneEnabled=true,modeAction=false,cleanSpaces=false,showCancel=false;
};
enum class KeyboardEvent : uint8_t { None,Changed,Done,Back,Cancel,Mode };
// Allocation-free editor shared by URL, credentials, pet names and score names.
// Geometry is logical 240x240; the panel scales each key to 68x56 pixels.
class Keyboard {
 public:
  enum { kColumns=6,kLeft=8,kTop=54,kBottom=202,kWidth=34,kHeight=28,kPitchX=38,kPitchY=30 };
  static const char* asciiKeys();
  static char character(const KeyboardSpec&,int target);
  static int maxScroll(const KeyboardSpec&);
  static bool enabled(const KeyboardSpec&,int target);
  void reset(int focus=0);
  int focus() const { return focus_; }
  int scroll() const { return scroll_; }
  void next(const KeyboardSpec&);
  KeyboardEvent activate(const KeyboardSpec&,char* text,int target=-1);
  KeyboardEvent touch(const KeyboardSpec&,char* text,bool down,int x,int y);
  int hitTest(const KeyboardSpec&,int x,int y) const;
  void cancelTouch();
 private:
  int focus_=0,scroll_=0,startX_=0,startY_=0,startScroll_=0,target_=-1;
  bool touching_=false,dragged_=false,scrolling_=false,releaseRequired_=false;
  void reveal(const KeyboardSpec&);
};
void drawKeyboardRow(uint16_t* row,const KeyboardSpec&,const Keyboard&,unsigned y);
void drawKeyboard(uint16_t* pixels,const KeyboardSpec&,const Keyboard&);
// Shared case-sensitive font, also used for URL bars and network labels.
void drawKeyboardGlyphRow(uint16_t*,int row,int x,int y,char,uint16_t color,int scale=1);
void drawKeyboardText(uint16_t* pixels,int x,int y,const char*,uint16_t color,size_t limit);
}
