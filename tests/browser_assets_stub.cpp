// Transport/controller unit tests isolate assets; the real pipeline and codecs
// are exercised by browser-assets-test with the actual LHP library.
#include "../firmware/sloth_pet/browser_assets.h"
namespace sloth { namespace browser_assets {
Page* prepare(char*&,size_t&,const char*,Fetch,Cancel){return nullptr;}
Page* prepareReader(browser_reader::Document&,const char*,Fetch,Cancel){return nullptr;}
void release(Page*& p){p=nullptr;}
BrowserEngineAssets callbacks(Page*){return {};}
Stats stats(const Page*){return {};}
} }
