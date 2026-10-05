#pragma once
#include "browser_url.h"
#include <stddef.h>
#include <stdint.h>
namespace sloth { namespace browser_reader {struct Document;} namespace browser_assets {struct Page;} namespace browser_fetch {
constexpr size_t kMaxBody=32*1024,kMaxTextSource=512*1024;
enum class State:uint8_t { Idle,Running,Complete,Failed,Cancelled };
enum class Error:uint8_t { None,Url,Clock,Memory,Dns,Connect,Tls,Http,Headers,ContentType,Encoding,TooLarge,Redirect,Timeout,Cancelled,Storage };
struct Snapshot { State state; Error error; uint16_t httpStatus; uint32_t received; };
struct Result { browser_assets::Page* assets=nullptr; browser_reader::Document* reader=nullptr;char* bytes=nullptr;size_t length=0;char url[browser_url::kCapacity]{};bool secure=false; };
// Main-loop API. The caller exclusively owns connected Wi-Fi and sets system
// time before HTTPS. No redirects may downgrade HTTPS, bounded subresources use the same verified transport.
// Initialize persistent TLS accelerator mutexes before releasing the pet canvas.
void prepare();
// begin refuses an active worker or an unclaimed completed result.
bool begin(const char* url,bool textMode=false);
void cancel(); // Cooperative; never frees data/TLS while the worker uses it.
bool busy();
Snapshot snapshot();
// Last completed worker's minimum unused stack, in ESP-IDF bytes; zero while
// a new request is pending or when no worker has completed.
uint32_t stackLowWaterBytes();
// take succeeds once, only after TLS/socket cleanup and worker completion.
// The caller owns Result.bytes until release; keep it through renderer teardown.
bool take(Result& result);
void release(Result& result);
} }
