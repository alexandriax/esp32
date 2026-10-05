#pragma once
#include "browser_fetch.h"
#include "browser_http.h"
#include "browser_engine.h"
#include "storage_files.h"
#include "browser_reader.h"
namespace sloth { namespace browser_assets {
constexpr unsigned kImages=browser_image::kMaxImages,kStyles=2,kSlots=32,kImageSide=320;
constexpr size_t kCssBytes=8192,kImageBytes=512*1024,kHtmlBytes=64*1024;
enum class Kind { Css, Image, ReaderImage };
struct Download { bool jpeg=false;uint32_t maxAge=0; };
// Synchronous callbacks run only on the existing fetch worker. No engine exists
// during preparation. Socket/TLS cleanup finishes before download returns.
using Fetch=bool(*)(const char*,Kind,browser_fetch::detail::Writer&,size_t,Download&);
using Cancel=bool(*)();
struct Page;
Page* prepare(char*& html,size_t& length,const char* base,Fetch,Cancel);
Page* prepareReader(browser_reader::Document&,const char* base,Fetch,Cancel);
void release(Page*&);
BrowserEngineAssets callbacks(Page*);
struct Stats { unsigned images=0,styles=0,hits=0,skipped=0;bool sd=false; };
Stats stats(const Page*);
// Linewise decoder: reads FETCH.TMP, writes scaled RGBA to the already-open file.
// On any error the caller discards the incomplete file. No engine may be active.
bool decode(storage_files::File* input,storage_files::File* output,bool jpeg,
            unsigned& width,unsigned& height,Cancel,bool readerResolution=false);
} }
