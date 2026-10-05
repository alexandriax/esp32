#pragma once
#include <stddef.h>
#include <stdint.h>
#include "browser_engine.h"
#include "browser_viewport.h"
#include "browser_image_source.h"
namespace sloth { namespace browser_reader {
constexpr unsigned kRowHeight=10,kNativeRowHeight=kRowHeight*2;
constexpr unsigned kColumns=(240-BROWSER_PAGE_MARGIN*2)/6,kLines=512,kLinks=320,kUrlBytes=16384;
struct Span {uint16_t link;uint8_t first,last;};
struct Line {char text[kColumns];uint8_t length=0,spans=0;Span span[4]{};};
struct Image {
 uint16_t line=0,source=0,link=0,width=0,height=0;int16_t asset=-1;uint8_t pixelScale=1;
 unsigned logicalWidth() const {return (width*pixelScale+1)/2;}
 unsigned logicalHeight() const {return (height*pixelScale+1)/2;}
};
struct Document {
 // Grow with actual content instead of reserving every page's maximum while
 // TLS, the SD session and image decoder need the same limited SRAM.
 Document();~Document();
 Document(const Document&)=delete;Document& operator=(const Document&)=delete;
 Line* lines=nullptr;char* urls=nullptr;uint16_t offsets[kLinks]{};
 unsigned lineCapacity=0,urlCapacity=0;
 bool ensureLines(unsigned);bool ensureUrls(unsigned);
 uint16_t count=1,links=0,urlBytes=0;bool clipped=false;
 Image images[browser_image::kMaxImages]{};uint8_t imageCount=0;
 unsigned height() const;
 // Prepared after the HTML/TLS parser has finished; no image IO while parsing.
 void attachImages(const BrowserEngineAssets&,unsigned pixelScale=1);
 const char* link(unsigned id) const {return id && id<=links?urls+offsets[id-1]:nullptr;}
 int linkAt(unsigned x,unsigned y) const;
};
// Streaming HTML/Atom reader: bounded state, no DOM, scripts or styles. Records bounded image sources for the shared SD pipeline.
// Each input byte is consumed once. finish() completes pending text and paging.
class Parser {
 public:
 Parser(Document&,bool atom,const char* logicalUrl);
 ~Parser();
 Parser(const Parser&)=delete;Parser& operator=(const Parser&)=delete;
 bool valid() const {return state_!=nullptr;}
 void feed(const char*,size_t);void finish();
 private:
 struct State;State* state_;
};
void drawRow(uint16_t* row,const Document&,unsigned documentLogicalY,const BrowserEngineAssets* assets=nullptr);
// Uses Reddit's public feed for text reading; other origins are unchanged.
bool requestUrl(const char* logical,char* output,size_t capacity);
} }
