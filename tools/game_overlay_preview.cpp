#include "../firmware/sloth_pet/game_overlay.h"
#include "../firmware/sloth_pet/pet_canvas.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char** argv) {
  if(argc!=3) { fprintf(stderr,"usage: %s out.ppm confirm|name|name-middle|name-bottom|pong|pong2|tetris|tetris2|leaf|leaf2\n",argv[0]); return 2; }
  sloth::GameRecords records;
  const char* names[]={"MOSS","FERN","ALEX","SLOW COACH","LEAF","MIRA","O'LEAF","BASIL","RIVER","PIP"};
  for(unsigned i=0;i<10;++i) {
    sloth::addGameScore(records,sloth::GameKind::Pong,7,i*6/9,names[i]);
    sloth::addGameScore(records,sloth::GameKind::Tetris,45200-i*3800,84-i*7,names[i]);
    sloth::addGameScore(records,sloth::GameKind::LeafSweep,19680-i*1530,164-i*12,names[i]);
  }
  static uint16_t pixels[240*240];
  sloth::graphics::Canvas c{pixels};
  c.rect(0,0,240,240,sloth::graphics::ink);
  c.rect(0,35,240,185,sloth::graphics::rgb(10,28,26));
  for(int y=42;y<219;y+=12)c.rect(119,y,2,5,sloth::graphics::muted);
  c.rect(10,85,6,39,sloth::graphics::mint); c.rect(224,156,6,39,sloth::graphics::gold);
  c.oval(84,56,3,3,sloth::graphics::cream);
  c.text(26,12,"P1  4",sloth::graphics::mint); c.text(167,12,"2  P2",sloth::graphics::gold);
  sloth::GameOverlay ui;
  if(!strcmp(argv[2],"confirm"))ui.confirmExit(sloth::GameKind::Pong);
  else if(!strncmp(argv[2],"name",4)) {
    ui.enterName(sloth::GameKind::Tetris,45200,84,"MOSS");
    ui.activate(0);ui.activate(11);ui.activate(4);ui.activate(23);
    // Start at A rather than the last typed letter so previews show the initial viewport.
    for(int next=23;next!=0;next=(next+1)%41) ui.next();
    if(strcmp(argv[2],"name")) {
      const int end=!strcmp(argv[2],"name-bottom")?-1000:54;
      ui.touch(true,100,150);ui.touch(true,100,end);ui.touch(false,100,end);
    }
  }
  else {
    const auto game=!strncmp(argv[2],"leaf",4)?sloth::GameKind::LeafSweep:
        !strncmp(argv[2],"tetris",6)?sloth::GameKind::Tetris:sloth::GameKind::Pong;
    ui.showScores(game);if(strchr(argv[2],'2'))ui.activate();
  }
  sloth::drawGameOverlay(pixels,ui,records);
  FILE* file=fopen(argv[1],"wb");if(!file)return 1;
  fprintf(file,"P6\n240 240\n255\n");
  for(uint16_t p:pixels) { const unsigned char rgb[]={static_cast<unsigned char>(((p>>11)&31)*255/31),static_cast<unsigned char>(((p>>5)&63)*255/63),static_cast<unsigned char>((p&31)*255/31)};fwrite(rgb,1,3,file); }
  return fclose(file)?1:0;
}
