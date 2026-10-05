#include "browser_assets.h"
#include "browser_memory.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include "browser_ui.h"
using namespace sloth;
namespace storage_files {
struct File {std::string name;size_t pos=0;bool writable=false;};
static std::map<std::string,std::vector<uint8_t>> disk;
static bool mounted=false,available=true,failWrite=false;static int openCount=0;static unsigned readCalls=0;
bool mount(){if(!available||mounted)return false;mounted=true;return true;}
void unmount(){assert(!openCount);mounted=false;}
File* open(const char* name,Mode mode){assert(mounted);if(mode==Mode::Read&&!disk.count(name))return nullptr;
  auto* f=new File;f->name=name;f->writable=mode==Mode::Create;if(f->writable)disk[name].clear();++openCount;assert(openCount<=2);return f;}
size_t read(File* f,void* p,size_t n){++readCalls;assert(n<=4096);auto& data=disk[f->name];if(f->pos>=data.size())return 0;if(n>data.size()-f->pos)n=data.size()-f->pos;memcpy(p,data.data()+f->pos,n);f->pos+=n;return n;}
bool write(File* f,const void* p,size_t n){assert(f&&f->writable&&n<=4096);if(failWrite)return false;auto& data=disk[f->name];if(data.size()<f->pos+n)data.resize(f->pos+n);memcpy(data.data()+f->pos,p,n);f->pos+=n;return true;}
bool seek(File* f,uint32_t pos){f->pos=pos;return true;}
uint32_t size(File* f){return static_cast<uint32_t>(disk[f->name].size());}
bool close(File*& f){if(f){--openCount;delete f;f=nullptr;}return true;}
bool remove(const char* name){disk.erase(name);return true;}
bool rename(const char* from,const char* to){disk[to]=std::move(disk[from]);disk.erase(from);return true;}
}
static std::map<std::string,std::vector<uint8_t>> publicAssets;
static bool cancelled=false;static unsigned cancelChecks=0,cancelAfter=0;static unsigned downloads=0;static uint32_t ttl=3600;
static std::vector<uint8_t> jpg,png;static std::string css="body{margin:0;background:#0000ff} p{color:#ffffff;font-size:24px}";
static bool cancel(){return cancelled || (cancelAfter && ++cancelChecks>=cancelAfter);}
static bool fetch(const char* url,browser_assets::Kind kind,browser_fetch::detail::Writer& out,size_t limit,browser_assets::Download& result){
  assert(storage_files::openCount==1); // only download target, no retained images
  ++downloads;result.maxAge=ttl;std::vector<uint8_t> bytes;
  if(!publicAssets.empty()){
    auto found=publicAssets.find(url);if(found==publicAssets.end())return false;bytes=found->second;result.jpeg=bytes.size()>2&&bytes[0]==255&&bytes[1]==216;
  }else if(kind==browser_assets::Kind::Css)bytes.assign(css.begin(),css.end());
  else if(strstr(url,"image.png")){bytes=png;result.jpeg=false;}
  else if(strstr(url,"image.jpg")){bytes=jpg;result.jpeg=true;}
  else return false;
  if(bytes.size()>limit)return false;
  for(size_t i=0;i<bytes.size();){size_t n=bytes.size()-i;if(n>997)n=997;out.write(reinterpret_cast<const char*>(bytes.data()+i),n);if(out.failed())return false;i+=n;}return true;
}
static std::vector<uint8_t> load(const char* file){
  auto* f=fopen(file,"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);std::vector<uint8_t> data(n);assert(fread(data.data(),1,n,f)==n);fclose(f);return data;
}
static std::vector<uint8_t> pixels(480*BROWSER_VIEW_HEIGHT*3);static unsigned rows=0;
extern "C" int browser_panel_begin(){rows=0;return 1;}
extern "C" int browser_panel_line(unsigned y,const uint8_t* p,size_t n){assert(y==rows++);memcpy(pixels.data()+y*n,p,n);return 1;}
extern "C" int browser_panel_finish(){assert(rows==BROWSER_VIEW_HEIGHT);return 1;}
static browser_assets::Page* prepare(const char* source,char*& html,size_t& length){length=strlen(source);html=static_cast<char*>(browser_memory_malloc(length+1));memcpy(html,source,length+1);return browser_assets::prepare(html,length,"http://example.org/base/",fetch,cancel);}
static void release(browser_assets::Page*& page,char* html){browser_engine_stop();browser_assets::release(page);browser_memory_free(html);assert(!storage_files::openCount&&!storage_files::mounted&&!browser_memory_live());}
static void render(char* html,size_t length,browser_assets::Page* page){const auto assets=browser_assets::callbacks(page);assert(!browser_engine_start_assets(html,length,128*1024,&assets));for(unsigned i=0;i<10000;++i){auto state=browser_engine_service();if(state==BROWSER_ENGINE_READY)return;assert(state!=BROWSER_ENGINE_FAILED);}assert(false);}
int main(int argc,char** argv){
  if(argc==6){
    std::ifstream input(argv[1],std::ios::binary),manifest(argv[3]);assert(input&&manifest);
    const std::string source((std::istreambuf_iterator<char>(input)),{});std::string entry;
    while(std::getline(manifest,entry)){const size_t tab=entry.find('\t');assert(tab!=entry.npos);publicAssets[entry.substr(0,tab)]=load(entry.substr(tab+1).c_str());}
    auto* doc=new browser_reader::Document;
    {browser_reader::Parser parser(*doc,false,argv[2]);assert(parser.valid());for(size_t at=0;at<source.size();at+=137)parser.feed(source.data()+at,std::min(size_t(137),source.size()-at));parser.finish();}
    auto* page=browser_assets::prepareReader(*doc,argv[2],fetch,cancel);const auto stats=browser_assets::stats(page);const auto cb=browser_assets::callbacks(page);
    const unsigned offset=strtoul(argv[5],nullptr,10);BrowserUi ui;ui.show(argv[2]);ui.setHasPage(true);char status[40];snprintf(status,sizeof(status),"READ: %u IMAGES %u SKIPPED",stats.images,stats.skipped);ui.setStatus(status);
    auto* out=fopen(argv[4],"wb");assert(out);fprintf(out,"P6\n480 480\n255\n");
    for(unsigned y=0;y<480;++y){uint16_t row[240];if(browserChromeRow(y/2))drawBrowserRow(row,ui,y/2);else browser_reader::drawRow(row,*doc,offset/2+y/2-BROWSER_PAGE_TOP,&cb);
      for(unsigned x=0;x<480;++x){unsigned c=row[x/2];const uint8_t rgb[]={static_cast<uint8_t>(((c>>11)&31)*255/31),static_cast<uint8_t>(((c>>5)&63)*255/63),static_cast<uint8_t>((c&31)*255/31)};fwrite(rgb,1,3,out);}}
    fclose(out);printf("Public reader: source=%zu lines=%u links=%u image_blocks=%u images=%u skipped=%u height=%u scroll=%u\n",source.size(),doc->count,doc->links,doc->imageCount,stats.images,stats.skipped,doc->height(),offset);
    for(unsigned i=0;i<doc->imageCount;++i)printf("  image block %u: asset=%d width=%u height=%u\n",i,doc->images[i].asset,doc->images[i].width,doc->images[i].height);
    browser_assets::release(page);delete doc;assert(!browser_memory_live()&&!storage_files::openCount);return 0;
  }

  jpg=load(FIXTURE_DIR "/jpeg240-texture.jpg");png=load(FIXTURE_DIR "/browser-alpha.png");
  const char* source="<html><head><link rel='stylesheet' href='/style.css'></head><body><a href='/click'><img src='/image.png'></a><p>Cached pictures</p><img src='/image.jpg'></body></html>";
  char* html;size_t length;auto* page=prepare(source,html,length);auto stats=browser_assets::stats(page);
  printf("first: sd=%d styles=%u images=%u skipped=%u downloads=%u\n",stats.sd,stats.styles,stats.images,stats.skipped,downloads);fflush(stdout);
  assert(stats.sd&&stats.styles==1&&stats.images==2&&!stats.skipped&&downloads==3);
  assert(strstr(html,"<style>body")&&!strstr(html,"<link"));
  auto cb=browser_assets::callbacks(page);unsigned w,h;int id=cb.lookup(cb.user,"/image.png",&w,&h);assert(id>=0&&w==80&&h==40);
  const auto* rgba=cb.row(cb.user,id,0);assert(rgba&&rgba[0]==0&&rgba[1]==255&&rgba[2]==0&&rgba[3]==128);
  const unsigned reads=storage_files::readCalls;assert(cb.row(cb.user,id,1)&&storage_files::readCalls==reads); // shared read-ahead
  render(html,length,page);assert(pixels[18]==0&&pixels[19]==128&&pixels[20]==127);
  char link[512];assert(browser_engine_link_at(20,20,link,sizeof(link))==1&&!strcmp(link,"/click"));
  auto* preview=fopen("/tmp/moss-assets-preview.ppm","wb");assert(preview);fprintf(preview,"P6\n480 %u\n255\n",BROWSER_VIEW_HEIGHT);fwrite(pixels.data(),1,pixels.size(),preview);fclose(preview);
  for(int i=0;i<3;++i){assert(!browser_engine_scroll(0));for(unsigned j=0;j<1000&&browser_engine_service()!=BROWSER_ENGINE_READY;++j){}assert(pixels[19]==128);}
  release(page,html);
  page=prepare(source,html,length);stats=browser_assets::stats(page);assert(stats.hits==3&&downloads==3);release(page,html);
  // Exact file length is checked; a power-interrupted cache entry is refetched.
  for(auto& item:storage_files::disk)if(item.second.size()>600){item.second.resize(600);break;}
  page=prepare(source,html,length);assert(downloads==4);release(page,html);
  storage_files::disk.clear();ttl=0;page=prepare(source,html,length);release(page,html);assert(storage_files::disk.empty());ttl=3600;
  // Missing SD and short/failed writes leave the HTML usable and close all files.
  storage_files::available=false;page=prepare(source,html,length);assert(!browser_assets::stats(page).sd&&!strcmp(html,source));release(page,html);storage_files::available=true;
  storage_files::failWrite=true;page=prepare(source,html,length);assert(browser_assets::stats(page).skipped==3);release(page,html);storage_files::failWrite=false;
  // Truncated/malformed and oversized PNG never publish a decoded cache entry.
  auto saved=png;png.resize(png.size()-8);page=prepare("<img src='/image.png'>",html,length);assert(browser_assets::stats(page).images==0);release(page,html);
  png=saved;png[16]=1;page=prepare("<img src='/image.png'>",html,length);assert(browser_assets::stats(page).images==0);release(page,html);png=saved;
  png=load(FIXTURE_DIR "/browser-scaled.png");storage_files::disk.clear();
  page=prepare("<img src='/image.png'>",html,length);cb=browser_assets::callbacks(page);id=cb.lookup(cb.user,"/image.png",&w,&h);
  assert(id>=0&&w==320&&h==160);rgba=cb.row(cb.user,id,159);assert(rgba&&rgba[0]==20&&rgba[1]==90&&rgba[2]==180&&rgba[3]==255);release(page,html);
  storage_files::disk.clear();cancelAfter=30;cancelChecks=0;
  page=prepare("<img src='/image.png'>",html,length);assert(browser_assets::stats(page).images==0);release(page,html);assert(storage_files::disk.empty());cancelAfter=0;png=saved;

  for(unsigned depth:{1u,2u,4u,8u}){
    char path[512];snprintf(path,sizeof(path),"%s/browser-indexed%u.png",FIXTURE_DIR,depth);png=load(path);storage_files::disk.clear();
    page=prepare("<img src='/huge.png' srcset='/image.png 1x, /huge.png 2x'>",html,length);cb=browser_assets::callbacks(page);id=cb.lookup(cb.user,"/huge.png",&w,&h);
    assert(id>=0&&w==13&&h==11);rgba=cb.row(cb.user,id,0);assert(rgba&&rgba[0]==0&&rgba[1]==255&&rgba[2]==40&&rgba[3]==128);
    rgba=cb.row(cb.user,id,10);assert(rgba&&rgba[0]==(10%(1u<<depth))*255/((1u<<depth)-1));release(page,html);
    auto valid=png;png[44]^=1;storage_files::disk.clear();page=prepare("<img src='/image.png'>",html,length);assert(!browser_assets::stats(page).images);release(page,html);png=valid;
  }
  png=saved;storage_files::disk.clear();
  // Reader and HTML share the same cache, codec and cancellation ownership.
  {browser_reader::Document doc;const char* source="<p>Above</p><a href='/click'><img src='/image.png'></a><p>Below</p>";
    {browser_reader::Parser parser(doc,false,"http://example.org/");parser.feed(source,strlen(source));parser.finish();}
    page=browser_assets::prepareReader(doc,"http://example.org/",fetch,cancel);assert(browser_assets::stats(page).images==1&&doc.images[0].asset>=0&&doc.images[0].width==40&&doc.images[0].logicalWidth()==40);
    cb=browser_assets::callbacks(page);uint16_t row[242];row[0]=row[241]=1234;browser_reader::drawRow(row+1,doc,13,&cb);
    assert(row[0]==1234&&row[241]==1234&&row[101]!=0xffff);assert(doc.linkAt(210,26)==doc.images[0].link);
    browser_assets::release(page);assert(!storage_files::openCount);
  }
  // Do not fetch apparent assets inside comments / raw text, or unsafe schemes.
  unsigned before=downloads;page=prepare("<!-- <img src='/image.png'> --><script>let x=\"<img src='/image.jpg'>\";</script><textarea><img src='/image.png'></textarea><img src='file:/secret'><link rel='stylesheet' href='data:text/css,bad'>",html,length);assert(downloads==before);release(page,html);
  cancelled=true;page=prepare(source,html,length);assert(downloads==before);release(page,html);cancelled=false;
  // CSS crosses disk read chunks and cannot inject HTML via a style terminator.
  storage_files::disk.clear();css="body{color:red}/*"+std::string(6000,' ')+"</style><img src='/image.png'>*/";
  page=prepare("<link href='/style.css' rel='stylesheet'><body>text</body>",html,length);assert(browser_assets::stats(page).styles==1&&strstr(html,"\\3c /style>"));release(page,html);
  assert(storage_files::disk.size()<=browser_assets::kSlots);
  puts("Browser assets: real JPEG/PNG decode, CSS, alpha, links, cache reuse, corruption, no-store, cancellation and cleanup passed");
}
