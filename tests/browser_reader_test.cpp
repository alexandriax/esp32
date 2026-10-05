#include "../firmware/sloth_pet/browser_reader.h"
#include "../firmware/sloth_pet/browser_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace sloth::browser_reader;
std::string text(const Document& d){std::string s;for(unsigned i=0;i<d.count;++i){s.append(d.lines[i].text,d.lines[i].length);s+='\n';}return s;}
void parse(Document& d,const std::string& s,bool atom=false,size_t chunk=1,const char* url="https://reddit.com/r/test/"){
 Parser p(d,atom,url);assert(p.valid());for(size_t i=0;i<s.size();i+=chunk)p.feed(s.data()+i,s.size()-i<chunk?s.size()-i:chunk);p.finish();
}
int main(int argc,char** argv){
 const std::string html="<html><head><title>Hidden</title><style>invisible { x: 1 }</style></head><body><!--comment--><script>const x = '<tag>'; secret</script><h1>Readable &amp; useful</h1><p>A <a href='item?id=42&amp;sort=top'>linked title</a> and <a href='javascript:bad()'>plain text</a>.</p><p>UTF-8: \xe2\x80\x9cHello\xe2\x80\x9d &#8212; end.</p></body></html>";
 for(unsigned chunk:{1u,2u,7u,1024u}){Document d;parse(d,html,false,chunk);auto t=text(d);assert(t.find("Readable & useful")!=t.npos&&t.find("secret")==t.npos&&t.find("Hidden")==t.npos&&t.find("comment")==t.npos);assert(t.find("\"Hello\" - end.")!=t.npos);assert(d.links==1&&!strcmp(d.link(1),"item?id=42&sort=top"));bool found=false;for(unsigned y=0;y<d.count;++y)for(unsigned x=0;x<40;++x)if(d.linkAt(x*12,y*kNativeRowHeight)==1)found=true;assert(found&&!d.linkAt(480,0)&&!d.linkAt(0,999999));}
 const std::string atom="<?xml version='1.0'?><feed><title>Not an entry</title><entry><content type='html'>&lt;p&gt;Post &amp;amp; body &lt;a href=&quot;https://example.com/story&quot;&gt;Article&lt;/a&gt;&lt;/p&gt;</content><id>t3_abc</id><link href='https://reddit.com/r/test/comments/abc/topic/'/><title>A \xe2\x80\x9cpost\xe2\x80\x9d &#8212; title</title></entry><entry><content type='html'><![CDATA[<p>Second body</p>]]></content><id>t3_def</id><link href='https://reddit.com/r/test/comments/def/topic/'/><title>Second</title></entry></feed>";
 for(unsigned chunk:{1u,3u,29u,1024u}){Document d;parse(d,atom,true,chunk);auto t=text(d);assert(t.find("A \"post\" - title")!=t.npos&&t.find("Post & body")!=t.npos&&t.find("Second body")!=t.npos&&t.find("MORE POSTS >")!=t.npos);assert(d.links==4&&!strcmp(d.link(4),"https://reddit.com/r/test/?after=t3_def"));assert(t.find("Not an entry")==t.npos);}
 {Document d;parse(d,atom,true,5,"https://reddit.com/r/test/comments/abc/topic/");assert(text(d).find("MORE POSTS")==std::string::npos);}
 {Document d;parse(d,"<script>not </scripture> visible</SCRIPT  >readable");assert(text(d)=="readable\n");}
 {Document d;parse(d,"<script>loader()</script>");assert(text(d).find("No readable text.")!=std::string::npos);}
 {Document d;std::string huge;for(unsigned i=0;i<9000;++i)huge+="<p>long paragraph with enough words to wrap safely</p>";parse(d,huge,false,1024);assert(d.clipped&&d.count==kLines);}
 {Document d;std::string urls;for(unsigned i=0;i<400;++i)urls+="<a href='/"+std::to_string(i)+"'>link</a> ";parse(d,urls,false,2);assert(d.clipped&&d.links==kLinks);}
 {Document d;parse(d,"<a href='https://example.com/"+std::string(1200,'x')+"'>truncated</a>");assert(d.links==0);}
 {char out[512];assert(requestUrl("https://reddit.com",out,sizeof(out))&&!strcmp(out,"https://www.reddit.com/.rss?limit=12"));assert(requestUrl("https://old.reddit.com/r/test/?after=t3_abc&limit=100",out,sizeof(out))&&!strcmp(out,"https://www.reddit.com/r/test/.rss?after=t3_abc&limit=12"));assert(requestUrl("https://news.ycombinator.com/",out,sizeof(out))&&!strcmp(out,"https://news.ycombinator.com/"));assert(!requestUrl("javascript:bad()",out,sizeof(out)));assert(!requestUrl("https://reddit.com",out,10));}
 {Document d;parse(d,html);uint16_t pixels[242];pixels[0]=pixels[241]=1234;for(unsigned y=0;y<d.height()/2+10;++y){drawRow(pixels+1,d,y);assert(pixels[0]==1234&&pixels[241]==1234);}}

 // Responsive, entity-escaped URLs and tags larger than the old 1536-byte cap.
 {Document d;parse(d,"<p>Before</p><a href='/photo'><img "+std::string(1700,' ')+"src='/huge.png' srcset='/small.png?a=1&amp;b=2 96w, /medium.png 320w, /huge.png 3840w' alt='Photo'></a><a href='/after'>After</a>",false,7);
  assert(d.imageCount==1&&!strcmp(d.link(d.images[0].source),"/medium.png"));
  BrowserEngineAssets assets{nullptr,[](void*,const char*,unsigned* w,unsigned* h)->int{*w=80;*h=40;return 0;},nullptr};
  const unsigned plain=d.height();d.attachImages(assets);assert(d.height()==plain+32);
  assert(d.linkAt(220,26)==d.images[0].link);assert(!d.linkAt(0,26));
  assert(d.linkAt(BROWSER_PAGE_MARGIN*2,72)==d.links); // text below expanded image
 }
 {char out[512];using sloth::browser_image::source;
  const char* tag="img width='44' src='/bad' srcset='/one.png 1x, /two.png 2x'";
  assert(source(tag,strlen(tag),out)&&!strcmp(out,"/one.png"));
  tag="img src='/fallback' srcset='/bad -2w, /bad2 nonsense'";assert(source(tag,strlen(tag),out)&&!strcmp(out,"/fallback"));
  tag="img srcset='/a 96w, /b 256w'";assert(source(tag,strlen(tag),out)&&!strcmp(out,"/b"));
  tag="img srcset='/a, /b 2x'";assert(source(tag,strlen(tag),out)&&!strcmp(out,"/a"));
 }
 {Document d;parse(d,"<img src='image.svg'><img src='x.png' width=1 height=1><img src='data:image/png,AAAA'>");assert(!d.imageCount);}
 // Small pages must leave RAM for the next verified image TLS handshake.
 {const size_t before=browser_memory_live();{Document d;parse(d,"<a href='/project'>Small page</a>");assert(sizeof(d)+browser_memory_live()-before<4096);}assert(browser_memory_live()==before);}
 // Optional local, public fixtures are not checked into the repository.
 if(argc==4){std::ifstream input(argv[1],std::ios::binary);assert(input);std::string source((std::istreambuf_iterator<char>(input)),{});Document d;parse(d,source,!strcmp(argv[2],"atom"),137);assert(d.count>30&&d.links>10);std::ofstream out(argv[3],std::ios::binary);out<<"P6\n480 "<<d.height()<<"\n255\n";uint16_t row[240];for(unsigned y=0;y<d.height();++y){drawRow(row,d,y/2);for(unsigned x=0;x<480;++x){unsigned c=row[x/2];char rgb[]={static_cast<char>(((c>>11)&31)*255/31),static_cast<char>(((c>>5)&63)*255/63),static_cast<char>((c&31)*255/31)};out.write(rgb,3);}}printf("Public fixture: %zu source bytes, %u lines, %u links, clipped=%u\n",source.size(),d.count,d.links,d.clipped);}
 assert(browser_memory_live()==0);puts("reader: chunk boundaries, Atom, HTML, link safety, UTF-8, clipping, URL mapping and bounded rendering passed");
}
