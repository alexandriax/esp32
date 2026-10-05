#include "../firmware/sloth_pet/game_overlay.h"
#include "../firmware/sloth_pet/pet_canvas.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
using namespace sloth;
namespace {
GameOverlayEvent tap(GameOverlay& ui, int x, int y) {
  const int target = ui.hitTest(x,y);
  assert(target >= 0);
  assert(ui.touch(true,x,y)==GameOverlayEvent::None);
  return ui.touch(false,x,y);
}
void navigation() {
  GameOverlay ui;
  assert(!ui.isOpen() && ui.targetCount() == 0 && ui.hitTest(50,15) == -1);
  ui.next(); assert(ui.activate() == GameOverlayEvent::None);
  ui.confirmExit(GameKind::Pong);
  assert(ui.game() == GameKind::Pong && ui.page() == GameOverlayPage::ConfirmExit);
  assert(ui.focus() == 0 && ui.targetCount() == 2);
  assert(ui.activate() == GameOverlayEvent::Resume && !ui.isOpen());
  ui.confirmExit(GameKind::Tetris);
  ui.next(); assert(ui.focus() == 1);
  assert(ui.back() == GameOverlayEvent::Resume && !ui.isOpen()); // PWR resumes from either choice.
  ui.confirmExit(GameKind::Pong);
  assert(tap(ui,50,15) == GameOverlayEvent::None && ui.focus() == 1);
  assert(tap(ui,190,15) == GameOverlayEvent::Leave && !ui.isOpen());
  ui.confirmExit(GameKind::Pong);
  assert(tap(ui,100,15) == GameOverlayEvent::Resume && !ui.isOpen());
  ui.confirmExit(GameKind::Tetris);
  assert(tap(ui,90,140) == GameOverlayEvent::Resume);
  ui.confirmExit(GameKind::Tetris);
  assert(tap(ui,90,170) == GameOverlayEvent::Leave);
  ui.confirmExit(GameKind::Pong);
  assert(ui.activate(-2) == GameOverlayEvent::None);
  assert(ui.activate(2) == GameOverlayEvent::None);
  assert(ui.activate(999) == GameOverlayEvent::None && ui.focus() == 0);
  assert(ui.hitTest(19,15) == -1 && ui.hitTest(220,15) == -1);
  assert(ui.hitTest(20,7) == -1 && ui.hitTest(20,24) == -1);
  assert(ui.hitTest(85,15) == GameOverlay::kNext);
  assert(ui.hitTest(86,15) == GameOverlay::kBack);
  assert(ui.hitTest(153,15) == GameOverlay::kBack);
  assert(ui.hitTest(154,15) == GameOverlay::kSelect);
}
void names() {
  GameOverlay ui;
  ui.enterName(GameKind::Pong,7,2,"MOSS");
  assert(ui.page() == GameOverlayPage::Name && ui.focus() == 39 && ui.targetCount() == 41);
  assert(ui.result().score == 7 && ui.result().detail == 2);
  assert(!*ui.name() && !strcmp(ui.submittedName(),"MOSS"));
  assert(ui.activate() == GameOverlayEvent::Save && ui.isOpen());
  assert(tap(ui,148,218) == GameOverlayEvent::Skip && ui.isOpen());
  // Hardware navigation starts at Save, then Skip, then A; Select adds A.
  ui.enterName(GameKind::Tetris,1234,12,"FERN");
  ui.next(); assert(ui.focus()==40); ui.next(); assert(ui.focus()==0);
  assert(ui.activate()==GameOverlayEvent::None && !strcmp(ui.name(),"A"));
  assert(tap(ui,60,68)==GameOverlayEvent::None && !strcmp(ui.name(),"AB"));
  ui.activate(36); assert(!strcmp(ui.name(),"AB "));
  ui.activate(36); assert(!strcmp(ui.name(),"AB ")); // No repeated spaces.
  ui.touch(true,100,74); ui.touch(true,100,2000); ui.touch(false,100,2000);
  tap(ui,100,74); assert(!strcmp(ui.name(),"AB C"));
  tap(ui,34,218); assert(!strcmp(ui.name(),"AB ")); // Delete.
  tap(ui,91,218); assert(!*ui.name()); // Clear.
  ui.activate(36); assert(!*ui.name()); // No leading space.
  for(unsigned i=0;i<10;++i) ui.activate(i);
  assert(!strcmp(ui.name(),"ABCDEFGHIJ"));
  ui.activate(10); ui.activate(36); assert(strlen(ui.name())==10);
  ui.activate(37); ui.activate(35); assert(!strcmp(ui.name(),"ABCDEFGHI9"));
  assert(ui.activate(39)==GameOverlayEvent::Save);
  GameRecords records;
  assert(addGameScore(records,ui.game(),ui.result().score,ui.result().detail,ui.submittedName()));
  assert(!strcmp(records.tables[1].entries[0].name,"ABCDEFGHI9"));
  ui.enterName(GameKind::Tetris,0,0,nullptr);
  assert(!*ui.name() && !strcmp(ui.submittedName(),"MOSS"));
  assert(ui.back()==GameOverlayEvent::Closed && !ui.isOpen());
  // Every touch key maps to the same character as hardware selection.
  for(int key=0;key<36;++key) {
    ui.enterName(GameKind::Pong,7,0,"");
    ui.next(); ui.next();
    for(int next=0;next<key;++next) ui.next();
    const int left=8+(key%6)*38, top=54+(key/6)*30-ui.scrollOffset();
    assert(top>=54 && top+28<=202);
    for(int y=top;y<top+28;++y) for(int x=left;x<left+34;++x) assert(ui.hitTest(x,y)==key);
    assert(tap(ui,left+18,top+14)==GameOverlayEvent::None);
    const char expected = key<26 ? 'A'+key : '0'+key-26;
    assert(ui.name()[0]==expected && ui.name()[1]==0);
  }
  // Optional fallback must always be storable even for arbitrary pet names.
  const char* fallbacks[] = {"", "   ", "Moss!", "fern leaf  long", "!!!", "lowercase", "   Moss   ", "O'LEAF-7"};
  for(const char* fallback : fallbacks) {
    ui.enterName(GameKind::Pong,7,0,fallback);
    GameRecords fresh;
    assert(strlen(ui.submittedName())<=kScoreNameLength);
    assert(addGameScore(fresh,GameKind::Pong,7,0,ui.submittedName()));
  }
}
void gestures() {
  GameOverlay ui;
  ui.enterName(GameKind::LeafSweep,12000,90,"MOSS");
  for(unsigned i=0;i<100;++i) assert(ui.touch(true,40,68)==GameOverlayEvent::None);
  assert(!*ui.name()); // Holding never repeats or commits a key.
  assert(ui.touch(false,40,68)==GameOverlayEvent::None && !strcmp(ui.name(),"A"));
  for(unsigned i=0;i<10;++i) ui.touch(false,40,68);
  assert(!strcmp(ui.name(),"A")); // Only the first release commits.
  tap(ui,60,68); tap(ui,100,74); assert(!strcmp(ui.name(),"ABC")); // Fast successive taps.
  ui.touch(true,30,68); ui.touch(true,36,74); ui.touch(false,36,74);
  assert(!strcmp(ui.name(),"ABCA")); // Modest diagonal drift is tolerated.
  ui.touch(true,40,68); ui.touch(false,44,68);
  assert(!strcmp(ui.name(),"ABCA")); // Release in the inter-key gap does not type a key.
  ui.touch(true,40,68); ui.touch(false,47,68);
  assert(!strcmp(ui.name(),"ABCA")); // Never reinterpret drift as the adjacent B.
  ui.touch(true,40,68); ui.touch(true,60,68); ui.touch(true,40,68); ui.touch(false,40,68);
  assert(!strcmp(ui.name(),"ABCA")); // A drag stays cancelled even if it returns.
  assert(ui.scrollOffset()==0);
  ui.touch(true,40,68); ui.touch(false,-1,68); assert(!strcmp(ui.name(),"ABCA"));

  ui.touch(true,100,150); ui.touch(true,100,90);
  assert(ui.scrollOffset()==60 && !strcmp(ui.name(),"ABCA"));
  ui.touch(false,100,90); assert(ui.scrollOffset()==60); // The drag follows the finger.
  assert(ui.hitTest(40,94)==18); tap(ui,40,94); assert(!strcmp(ui.name(),"ABCAS"));
  ui.touch(true,100,150); ui.touch(true,100,-1000); ui.touch(false,100,-1000);
  assert(ui.scrollOffset()==GameOverlay::kMaxScroll);
  assert(ui.hitTest(210,158)==35); tap(ui,210,158); assert(!strcmp(ui.name(),"ABCAS9"));
  // Fixed editing and submit controls do not move with the keyboard.
  assert(tap(ui,34,218)==GameOverlayEvent::None && !strcmp(ui.name(),"ABCAS"));
  assert(tap(ui,205,218)==GameOverlayEvent::Save);
  assert(tap(ui,148,218)==GameOverlayEvent::Skip);
  ui.touch(true,34,218); ui.touch(true,110,100); ui.touch(false,110,100);
  assert(ui.scrollOffset()==GameOverlay::kMaxScroll && !strcmp(ui.name(),"ABCAS"));
  ui.touch(true,100,90); ui.touch(true,100,740); ui.touch(false,100,740);
  assert(ui.scrollOffset()==0);

  // Hardware selection reveals each row and wraps back to the first letter.
  ui.enterName(GameKind::Pong,7,0,"MOSS"); ui.next(); ui.next();
  for(int key=0;key<36;++key) {
    assert(ui.focus()==key);
    const int y=54+(key/6)*30-ui.scrollOffset();
    assert(y>=54 && y+28<=202);
    ui.next();
  }
  assert(ui.focus()==39); // Empty name skips Space, Delete and Clear.
  ui.next(); ui.next();
  assert(ui.focus()==0 && ui.scrollOffset()==0);

  ui.touch(true,40,68); ui.cancelTouch();
  ui.touch(true,40,68); ui.touch(false,40,68);
  assert(!*ui.name()); // Cancelled held contacts must release before a new gesture.
  tap(ui,40,68); assert(!strcmp(ui.name(),"A"));
  ui.touch(true,60,68); ui.next(); ui.touch(false,60,68);
  assert(!strcmp(ui.name(),"A")); // Hardware navigation cancels an in-flight touch.
  ui.enterName(GameKind::Tetris,100,1,"MOSS");
  ui.touch(true,40,68); ui.next(); // Hardware interrupts a held A.
  ui.cancelTouch(); ui.touch(false,40,68); // Root's global release gate forwards this release.
  ui.touch(true,60,68); ui.touch(false,60,68);
  assert(!strcmp(ui.name(),"B")); // The very next tap must work, with no extra release.
  ui.touch(true,60,68); ui.showScores(GameKind::Pong); ui.touch(false,60,68);
  assert(ui.page()==GameOverlayPage::Scores && ui.scorePage()==0);
  assert(tap(ui,50,215)==GameOverlayEvent::None && ui.scorePage()==1);
  ui.enterName(GameKind::Pong,7,0,"MOSS");
  ui.touch(true,INT_MIN,INT_MAX); ui.touch(false,INT_MAX,INT_MIN);
  assert(!*ui.name() && ui.scrollOffset()==0);
  ui.touch(true,100,120); ui.touch(true,INT_MIN,INT_MAX); ui.touch(false,INT_MIN,INT_MAX);
  assert(!*ui.name() && ui.scrollOffset()==0);

  // Scrolling is clipped to the keyboard: name, hint, rail and controls stay
  // exactly stable even while a partial row slides past the viewport edge.
  static uint16_t before[240*240], after[240*240];
  GameRecords records; ui.next(); ui.next();
  drawGameOverlay(before,ui,records);
  ui.touch(true,100,150); ui.touch(true,100,127);
  assert(ui.scrollOffset()==23); drawGameOverlay(after,ui,records);
  for(int y=0;y<240;++y) if(y<54 || y>=202)
    for(int x=0;x<240;++x) assert(before[y*240+x]==after[y*240+x]);
  ui.touch(false,100,127); assert(ui.scrollOffset()==23);
}
void scoresAndRendering() {
  GameRecords records;
  for(unsigned i=0;i<10;++i) {
    char name[11]; snprintf(name,sizeof(name),"PLAYER %u",i);
    assert(addGameScore(records,GameKind::Pong,7,i%7,name));
    assert(addGameScore(records,GameKind::Tetris,100000+i*12345,i*10,name));
    assert(addGameScore(records,GameKind::LeafSweep,5000+i*1500,40+i*11,name));
  }
  assert(addGameScore(records,GameKind::Tetris,UINT32_MAX,UINT16_MAX,"ABCDEFGHIJ"));
  assert(addGameScore(records,GameKind::LeafSweep,UINT32_MAX,UINT16_MAX,"ABCDEFGHIJ"));
  GameOverlay ui;
  static uint16_t pixels[240*240+2];
  const auto draw = [&]() {
    pixels[0]=0x1234; pixels[240*240+1]=0x5678;
    drawGameOverlay(nullptr,ui,records);
    drawGameOverlay(pixels+1,ui,records);
    assert(pixels[0]==0x1234 && pixels[240*240+1]==0x5678);
    // Every hit is a legal focus target or an explicit rail action.
    for(int y=-1;y<=240;++y) for(int x=-1;x<=240;++x) {
      const int t=ui.hitTest(x,y);
      assert(t==-1 || (t>=0 && t<ui.targetCount()) ||
             t==GameOverlay::kNext || t==GameOverlay::kBack || t==GameOverlay::kSelect);
      if(x<0 || x>=240 || y<0 || y>=240) assert(t==-1);
    }
  };
  for(unsigned game=0;game<kGameCount;++game) {
    ui.showScores(static_cast<GameKind>(game));
    assert(ui.focus()==0 && ui.scorePage()==0 && ui.targetCount()==2);
    draw();
    assert(tap(ui,50,215)==GameOverlayEvent::None && ui.scorePage()==1 && ui.focus()==0);
    draw();
    ui.activate(); assert(ui.scorePage()==0);
    ui.next(); assert(ui.focus()==1);
    assert(ui.activate()==GameOverlayEvent::Closed && !ui.isOpen());
    ui.showScores(static_cast<GameKind>(game));
    assert(ui.scorePage()==0 && ui.focus()==0);
    assert(tap(ui,170,215)==GameOverlayEvent::Closed);
    ui.showScores(static_cast<GameKind>(game));
    drawGameOverlay(pixels+1,ui,records,true,false);
    drawGameOverlay(pixels+1,ui,records,false,true);
    for(unsigned i=0;i<240*240;++i) pixels[i+1]=graphics::rgb(8,24,32);
    ui.confirmExit(static_cast<GameKind>(game));
    draw();
    // Confirmation card overlays only its bounded area and rail.
    assert(pixels[1+40*240+120]==graphics::rgb(8,24,32));
    assert(pixels[1+230*240+120]==graphics::rgb(8,24,32));
    ui.next(); draw();
    ui.enterName(static_cast<GameKind>(game),game?UINT32_MAX:7,game?UINT16_MAX:6,"MOSS");
    for(int focus=0;focus<ui.targetCount();++focus) { ui.activate(focus); draw(); }
  }
  // The third board has its own optional-name result and ranking, independent
  // of the Pong/Tetris pages that were rendered just above.
  ui.enterName(GameKind::LeafSweep,45678,137,"FERN");
  assert(ui.game()==GameKind::LeafSweep && ui.result().score==45678 && ui.result().detail==137);
  assert(ui.activate()==GameOverlayEvent::Save && !strcmp(ui.submittedName(),"FERN"));
  GameRecords leafResult;
  assert(addGameScore(leafResult,ui.game(),ui.result().score,ui.result().detail,ui.submittedName()));
  assert(leafResult.tables[2].count==1 && leafResult.tables[0].count==0 && leafResult.tables[1].count==0);
  ui.showScores(GameKind::Pong);
  GameRecords empty; drawGameOverlay(pixels+1,ui,empty);
  ui.close(); pixels[100]=0xbeef;
  drawGameOverlay(pixels+1,ui,records); assert(pixels[100]==0xbeef);
}
}
int main() {
  navigation(); names(); gestures(); scoresAndRendering();
  puts("Game overlay passed: Continue default, PWR resume, touch/rail/hardware keys, name limits and fallback, Save/Skip, large scrolling keys, immediate-release taps, hold/drift/swipe cancellation, three independent paged scoreboards, all-focus render bounds");
}
