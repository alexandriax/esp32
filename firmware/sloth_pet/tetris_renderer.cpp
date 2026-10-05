#include "tetris_game.h"
#include "tetris_controls.h"
#include "pet_canvas.h"
#include "ui_icons.h"

#include <stdio.h>

namespace sloth {
namespace {
using namespace graphics;
using namespace tetris_layout;
bool cell(uint16_t bits, int x, int y) { return (bits & (1u << (y * 4 + x))) != 0; }
uint16_t blockColor(unsigned piece) {
  const uint16_t colors[] = {mint, gold, rgb(174,157,214), rgb(141,184,101),
      rgb(220,134,117), rgb(116,175,193), rgb(209,166,106)};
  return colors[piece < 7 ? piece : 0];
}
void leafBlock(Canvas& c, int x, int y, int size, unsigned piece, bool ghost = false) {
  const uint16_t color = blockColor(piece);
  if (ghost) {
    c.rect(x, y, size - 1, size - 1, rgb(56,86,74));
    c.rect(x + 1, y + 1, size - 3, size - 3, rgb(9,24,23));
    c.dot(x + 1, y, color); c.dot(x + size - 3, y + size - 2, color);
    return;
  }
  c.rect(x, y, size - 1, size - 1, rgb(33,57,51));
  c.rect(x + 1, y + 1, size - 3, size - 3, color);
  c.line(x + 1, y, x + size - 3, y, cream);
  c.line(x, y + 1, x, y + size - 3, color);
  c.line(x + 1, y + size - 2, x + size - 2, y + size - 2, rgb(52,76,56));
  if (size >= 8) {
    c.line(x + 2, y + size - 4, x + size - 4, y + 2, rgb(236,239,190));
    c.dot(x + size - 4, y + 3, color);
  }
}
void boardBlock(Canvas& c, int column, int row, unsigned piece, bool ghost = false) {
  if (column < 0 || column >= kTetrisColumns || row < 0 || row >= kTetrisRows) return;
  leafBlock(c, kBoardLeft + column * kCell, kBoardTop + row * kCell, kCell, piece, ghost);
}
void mascot(Canvas& c, int x, int y, uint32_t now, bool cheering) {
  const bool blink = now % 4700 > 4460;
  if (cheering && now / 180 % 2) --y;
  c.line(x - 14, y - 9, x + 14, y - 9, furDark, 1);
  c.line(x - 8, y - 8, x - 6, y + 1, brown, 2);
  c.line(x + 8, y - 8, x + 6, y + 1, brown, 2);
  c.oval(x, y + 3, 10, 9, brown); c.oval(x, y + 4, 8, 7, face);
  c.line(x - 6, y + 1, x - 3, y + 3, mask, 1);
  c.line(x + 6, y + 1, x + 3, y + 3, mask, 1);
  if (blink) {
    c.line(x - 5, y + 2, x - 3, y + 2, ink); c.line(x + 3, y + 2, x + 5, y + 2, ink);
  } else {
    c.dot(x - 4, y + 2, cream); c.dot(x + 4, y + 2, cream);
    c.dot(x - 4, y + 3, ink); c.dot(x + 4, y + 3, ink);
  }
  c.oval(x, y + 4, 1, 1, mask);
  c.line(x - 2, y + 7, x, y + 8, mask); c.line(x, y + 8, x + 2, y + 7, mask);
  c.oval(x + 13, y - 10, 4, 2, mint);
  c.dot(x - 7, y - 4, furLight); c.dot(x + 6, y - 4, furLight);
  if (cheering) {
    c.dot(x - 13, y + 3, gold); c.dot(x + 13, y + 4, gold);
    c.line(x - 15, y - 3, x - 13, y - 5, mint);
  }
}
void rotateIcon(Canvas& c, int x, int y, bool pressed) {
  const uint16_t arc = pressed ? rgb(49,67,108) : rgb(174,157,214);
  const uint16_t tip = pressed ? rgb(48,90,65) : mint;
  c.line(x + 4, y + 12, x + 2, y + 10, arc, 1);
  c.line(x + 2, y + 10, x + 2, y + 6, arc, 1);
  c.line(x + 2, y + 6, x + 5, y + 3, arc, 1);
  c.line(x + 5, y + 3, x + 10, y + 3, arc, 1);
  c.line(x + 10, y + 3, x + 13, y + 6, arc, 1);
  c.line(x + 13, y + 6, x + 13, y + 9, arc, 1);
  c.triangle(x + 9, y + 7, x + 15, y + 7, x + 13, y + 12, tip);
}
void dropIcon(Canvas& c, int x, int y, bool pressed) {
  const uint16_t arrow = pressed ? rgb(48,90,65) : mint;
  const uint16_t floor = pressed ? rgb(122,73,22) : gold;
  c.rect(x + 6, y + 1, 4, 7, arrow);
  c.triangle(x + 3, y + 6, x + 12, y + 6, x + 8, y + 11, arrow);
  c.rect(x + 2, y + 13, 12, 2, floor);
}
void touchButton(Canvas& c, TetrisAction action, int pressed) {
  const auto rect = tetrisButtonRect(action);
  const bool active = pressed == static_cast<int>(action);
  const char* labels[] = {"", "LEFT", "RIGHT", "ROTATE", "DROP"};
  const uint16_t edge = active ? gold : rgb(72,110,85);
  c.roundRect(rect.x, rect.y, rect.width, rect.height, 5, edge);
  c.roundRect(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, 4,
              active ? mint : rgb(29,53,47));
  c.line(rect.x + 6, rect.y + 2, rect.x + rect.width - 7, rect.y + 2,
         active ? cream : rgb(85,121,96));
  c.line(rect.x + 5, rect.y + rect.height - 3, rect.x + rect.width - 6,
         rect.y + rect.height - 3, active ? rgb(78,131,93) : rgb(16,36,34));
  const int iconX = rect.x + (rect.width - 16) / 2, iconY = rect.y + 6;
  if (action == TetrisAction::Left || action == TetrisAction::Right)
    drawUiIcon(c, iconX, iconY, action == TetrisAction::Left ? UiIcon::Back : UiIcon::Next, active ? ink : mint);
  else if (action == TetrisAction::Rotate) rotateIcon(c, iconX, iconY, active);
  else dropIcon(c, iconX, iconY, active);
  const char* label = labels[static_cast<unsigned>(action)];
  c.text(rect.x + (rect.width - (static_cast<int>(strlen(label)) * 6 - 1)) / 2,
         rect.y + 29, label, active ? ink : cream);
}
void rail(Canvas& c, int pressed) {
  c.roundRect(20, 8, 200, 16, 3, mint);
  if (pressed == static_cast<int>(TetrisAction::Right)) c.roundRect(21, 9, 64, 14, 2, gold);
  if (pressed == static_cast<int>(TetrisAction::Pause)) c.rect(87, 9, 66, 14, gold);
  if (pressed == static_cast<int>(TetrisAction::Rotate)) c.roundRect(155, 9, 64, 14, 2, gold);
  c.rect(86, 10, 1, 12, ink); c.rect(154, 10, 1, 12, ink);
  c.text(41, 12, "MOVE", ink); c.text(105, 12, "PAUSE", ink); c.text(175, 12, "TURN", ink);
}
void nextPiece(Canvas& c, TetrisPiece piece) {
  const uint16_t bits = tetrisPieceMask(piece, 0);
  int minX = 4, maxX = -1, minY = 4, maxY = -1;
  for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) if (cell(bits, x, y)) {
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
  }
  if (maxX < minX) return;
  const int left = 156 - (maxX - minX + 1) * 6 / 2;
  const int top = 107 - (maxY - minY + 1) * 6 / 2;
  for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) if (cell(bits, x, y))
    leafBlock(c, left + (x - minX) * 6, top + (y - minY) * 6, 6, static_cast<unsigned>(piece));
}
}  // namespace

void drawTetris(uint16_t* pixels, const TetrisGame& game, int pressed, uint32_t now) {
  if (!pixels) return;
  Canvas c{pixels}; const auto& s = game.snapshot();
  c.rect(0, 0, 240, 240, ink);
  rail(c, pressed);
  c.centered(31, "3-TOED TETRIS", cream, 2);
  c.roundRect(kBoardLeft - 4, kBoardTop - 4, 88, 168, 3, rgb(46,62,44));
  c.roundRect(kBoardLeft - 3, kBoardTop - 3, 86, 166, 3, rgb(101,118,72));
  c.line(kBoardLeft - 2, kBoardTop, kBoardLeft - 2, kBoardTop + 157, rgb(168,177,115));
  c.rect(kBoardLeft, kBoardTop, 80, 160, rgb(9,24,23));
  for (int y = 0; y < kTetrisRows; ++y) for (int x = 0; x < kTetrisColumns; ++x) {
    if (s.board[y][x]) boardBlock(c, x, y, s.board[y][x] - 1);
    else c.dot(kBoardLeft + x * kCell + 3, kBoardTop + y * kCell + 3, rgb(25,43,34));
  }
  if (s.phase == TetrisPhase::Playing) {
    const uint16_t bits = tetrisPieceMask(s.piece, s.rotation);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) if (cell(bits, x, y)) {
      boardBlock(c, s.x + x, s.ghostY + y, static_cast<unsigned>(s.piece), true);
      boardBlock(c, s.x + x, s.y + y, static_cast<unsigned>(s.piece));
    }
  }
  char number[16];
  c.roundRect(108, 45, 114, 48, 4, rgb(24,46,40));
  c.line(112, 92, 217, 92, rgb(52,77,54));
  c.text(112, 49, "SCORE", muted);
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(s.score)); c.text(112, 60, number, cream);
  c.text(112, 74, "LINES", muted); c.text(184, 74, "LEVEL", muted);
  snprintf(number, sizeof(number), "%lu", static_cast<unsigned long>(s.lines)); c.text(112, 85, number, mint);
  snprintf(number, sizeof(number), "%u", s.level); c.text(190, 85, number, gold);
  c.text(112, 103, "NEXT", muted);
  c.roundRect(139, 97, 35, 21, 3, rgb(29,50,43));
  c.line(143, 98, 169, 98, rgb(64,91,64));
  nextPiece(c, s.next);
  mascot(c, 199, 107, now, s.lastClearedLines != 0);
  for (unsigned action = 1; action <= 4; ++action) touchButton(c, static_cast<TetrisAction>(action), pressed);
  if (s.phase == TetrisPhase::GameOver) {
    c.roundRect(27, 83, 188, 42, 5, rgb(6,19,17));
    c.roundRect(26, 81, 188, 42, 5, gold);
    c.roundRect(27, 82, 186, 40, 4, cream);
    c.centered(90, "LEAF PILE FULL", ink);
    c.centered(108, "TAP A BUTTON / OR PRESS", rgb(79,100,68));
  }
}
}  // namespace sloth
