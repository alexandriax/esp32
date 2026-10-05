#include "pet_renderer.h"
#include "pet_speech.h"
#include "pet_reaction.h"
#include "pet_ambient.h"
#include "pet_canvas.h"
#include "ui_icons.h"

#include <stddef.h>
#include <string.h>

namespace sloth {
namespace {

using namespace graphics;
const int kButtonLeft = 14;
const int kButtonTop = 198;
const int kButtonPitch = 62;
const int kButtonWidth = 56;
const int kMenuWidth = 28;
const int kButtonHeight = 23;
const int kTouchPaddingX = 3;
const int kTouchPaddingY = 8;

template <size_t N>
void hudLabel(char (&out)[N], const char* value, const char* fallback) {
  const char* source = value && value[0] ? value : fallback;
  size_t i = 0;
  for (; i + 1 < N && source[i]; ++i) out[i] = source[i];
  out[i] = '\0';
}

void drawHud(Canvas& c, const Hud& hud) {
  // A single row in each corner leaves the centered pet name untouched.
  char time[9];
  hudLabel(time, hud.timeText, "--:--");
  c.text(14, 15, time, cream);

  char level[5] = {'-', '-', '%', '\0', '\0'};
  const int percent = hud.batteryPercent > 100 ? 100 : hud.batteryPercent;
  const uint16_t batteryColor = percent >= 0 && percent < 20 ? gold : mint;
  c.roundRect(171, 14, 23, 11, 2, muted);
  c.rect(173, 16, 19, 7, ink);
  c.rect(194, 17, 2, 5, muted);
  if (hud.batteryPresent && percent >= 0) {
    const int fill = (percent * 17 + 50) / 100;
    c.rect(174, 17, fill, 5, batteryColor);
    unsigned i = 0;
    if (percent == 100) level[i++] = '1';
    if (percent >= 10) level[i++] = '0' + (percent / 10) % 10;
    level[i++] = '0' + percent % 10;
    level[i++] = '%';
    level[i] = '\0';
  }
  const int levelWidth = static_cast<int>(strlen(level)) * 6 - 1;
  c.text(224 - levelWidth, 15, level, cream);
  if (hud.externalPower) {
    // Power-connected indicator stays inside the icon, also for a full battery.
    c.line(183, 16, 179, 20, ink, 1);
    c.line(179, 20, 184, 20, ink, 1);
    c.line(184, 20, 181, 22, ink, 1);
    c.line(183, 16, 179, 20, gold);
    c.line(179, 20, 184, 20, gold);
    c.line(184, 20, 181, 22, gold);
  }
}

void leaf(Canvas& c, int x, int y, bool left, uint16_t color, int size = 9) {
  // The pointed ends and diagonal vein keep the foliage crisp at 240 px.
  const int direction = left ? -1 : 1;
  for (int step = 0; step <= size * 2; ++step) {
    int radius = step <= size ? step / 2 : (size * 2 - step) / 2;
    c.line(x + direction * step / 2 - radius, y - step,
           x + direction * step / 2 + radius, y - step, color);
  }
  c.line(x, y, x + direction * size, y - 2 * size, rgb(87, 143, 107));
}

void heart(Canvas& c, int x, int y, uint16_t color) {
  c.oval(x - 3, y - 2, 4, 4, color);
  c.oval(x + 3, y - 2, 4, 4, color);
  for (int row = 0; row < 8; ++row)
    c.rect(x - 7 + row, y + row, 15 - 2 * row, 1, color);
}

void meter(Canvas& c, int x, const char* title, uint8_t value, uint16_t color,
           bool charging = false, uint32_t animationMs = 0) {
  const unsigned clamped = value > 100 ? 100 : value;
  c.text(x, 173, title, muted);
  char percent[5];
  unsigned length = 0;
  if (clamped == 100) percent[length++] = '1';
  if (clamped >= 10) percent[length++] = '0' + (clamped / 10) % 10;
  percent[length++] = '0' + clamped % 10;
  percent[length++] = '%';
  percent[length] = '\0';
  c.text(x + 59 - static_cast<int>(length * 6 - 1), 173, percent, color);
  c.roundRect(x, 184, 59, 5, 2, rgb(47, 72, 64));
  const int filled = (clamped * 59 + 50) / 100;
  if (filled > 4) c.roundRect(x, 184, filled, 5, 2, color);
  else if (filled) c.rect(x, 185, filled, 3, color);
  if (charging && filled > 2) {
    const int shine = static_cast<int>((animationMs / 45) % filled);
    c.rect(x + shine, 185, 1, 3, cream);
    if (shine + 1 < filled) c.rect(x + shine + 1, 185, 1, 3, cream);
  }
}

// A small articulated pose drives all species. Coordinates are relative to the
// body center; a short ease-in/ease-out returns limbs smoothly to their perch.
struct ReactionPose {
  int id = -1, strength = 0, bodyX = 0, bodyY = 0, headX = 0, headY = 0, tilt = 0;
  int handX[2] = {-21, 21}, handY[2] = {119, 116};
  int elbowX[2] = {-34, 34}, elbowY[2] = {112, 112};
  bool happy = false, closed = false, openMouth = false;
  ReactionPose(bool asleep, int reaction, uint32_t ms) {
    if (asleep || reaction < 0 || reaction >= kPetReactionCount || ms >= kPetReactionDurationMs) return;
    id = reaction;
    const int phase = static_cast<int>(ms % 1000) * 32 / 1000;
    const int wave = phase < 16 ? phase - 8 : 24 - phase;
    const int lift = wave < 0 ? -wave : wave;
    switch (id) {
      case 0: // Shrug: both palms turn up with one raised eyebrow.
        handX[0] = -51; handX[1] = 51;
        handY[0] = 91 - lift / 2; handY[1] = 94 - lift / 2;
        elbowX[0] = -42; elbowX[1] = 42;
        elbowY[0] = elbowY[1] = 108; headY = 2; tilt = -3; break;
      case 1: // Wave: a high, waving right hand and a leaning head.
        handX[1] = 52 + wave; handY[1] = 68;
        elbowX[1] = 44; elbowY[1] = 91;
        headX = -3; tilt = -4; happy = true; break;
      case 2: // Facepalm: one hand actually crosses the face.
        handX[1] = -3; handY[1] = 72 + lift / 3;
        elbowX[1] = 38; elbowY[1] = 103;
        headY = 4; tilt = 5; closed = true; break;
      case 3: // Curious head tilt, with a hand beneath the chin.
        tilt = 8 + wave / 3; headX = 3;
        handX[1] = 12; handY[1] = 99;
        elbowX[1] = 37; elbowY[1] = 113; break;
      case 4: // Nod: the whole head dips while paws rest against the chest.
        headY = 3 + lift; closed = lift > 4;
        handX[0] = -14; handX[1] = 14;
        handY[0] = handY[1] = 108;
        elbowX[0] = -30; elbowX[1] = 30; break;
      case 5: // Stretch: long arms and open mouth, safely below the status.
        bodyY = -2; handX[0] = -47; handX[1] = 47;
        handY[0] = handY[1] = 59 + lift / 3;
        elbowX[0] = -43; elbowX[1] = 43;
        elbowY[0] = elbowY[1] = 85;
        closed = openMouth = true; break;
      case 6: // Peekaboo: both hands uncover the eyes in alternating beats.
        handX[0] = -15 - lift; handX[1] = 15 + lift;
        handY[0] = handY[1] = 78;
        elbowX[0] = -40; elbowX[1] = 40;
        elbowY[0] = elbowY[1] = 105; headY = 3; break;
      case 7: // Giggle: shoulders bounce and hands cover the smiling mouth.
        bodyY = lift / 3; headY = lift / 2;
        handX[0] = -8; handX[1] = 8;
        handY[0] = handY[1] = 96 + lift / 3;
        elbowX[0] = -31; elbowX[1] = 31;
        happy = openMouth = true; break;
      case 8: // Sway: the whole body rocks, with loose outstretched arms.
        bodyX = wave; tilt = -wave;
        handX[0] = -47; handX[1] = 47;
        handY[0] = 111 + wave; handY[1] = 111 - wave;
        elbowX[0] = -39; elbowX[1] = 39;
        elbowY[0] = 112 + wave / 2; elbowY[1] = 112 - wave / 2;
        happy = true; break;
      case 9: // Point: gaze and a straight finger follow the extended arm.
        handX[1] = 61; handY[1] = 87 + wave / 3;
        elbowX[1] = 43; elbowY[1] = 97;
        headX = 4; tilt = -4; break;
      case 10: // Heart hands: forearms meet at the heart over the chest.
        handX[0] = -7; handX[1] = 7;
        handY[0] = handY[1] = 107;
        elbowX[0] = -33; elbowX[1] = 33;
        elbowY[0] = elbowY[1] = 118;
        tilt = 3; happy = true; break;
      case 11: // Slow clap: the two palms meet and separate visibly.
        handX[0] = -7 - lift; handX[1] = 7 + lift;
        handY[0] = handY[1] = 106;
        elbowX[0] = -30; elbowX[1] = 30;
        elbowY[0] = elbowY[1] = 117;
        bodyY = lift / 5; happy = true; break;
    }
    int weight = ms < 300 ? static_cast<int>(ms * 256 / 300) : 256;
    if (ms > 3500) weight = static_cast<int>((4000 - ms) * 256 / 500);
    strength = weight;
    if (weight < 48) happy = closed = openMouth = false;
    bodyX = bodyX * weight / 256; bodyY = bodyY * weight / 256;
    headX = headX * weight / 256; headY = headY * weight / 256;
    tilt = tilt * weight / 256;
    for (int i = 0; i < 2; ++i) {
      const int sign = i == 0 ? -1 : 1;
      const int homeY = i == 0 ? 119 : 116;
      handX[i] = sign * 21 + (handX[i] - sign * 21) * weight / 256;
      handY[i] = homeY + (handY[i] - homeY) * weight / 256;
      elbowX[i] = sign * 34 + (elbowX[i] - sign * 34) * weight / 256;
      elbowY[i] = 112 + (elbowY[i] - 112) * weight / 256;
    }
  }
};

void applyAmbientPose(ReactionPose& pose, const AmbientPose& ambient, Animal animal) {
  pose.headX += ambient.headX; pose.headY += ambient.headY;
  pose.tilt += ambient.tilt; pose.closed = ambient.closed;
  if (!ambient.groom) return;
  // Species-specific grooming uses actual limbs and a head turn, not particles.
  pose.id = kPetReactionCount; // Internal pose; never a user-selectable reaction.
  pose.strength = ambient.groom;
  const int hand = animal == Animal::Cat ? 0 : 1;
  const int targetX = animal == Animal::Cat ? -18 : animal == Animal::Frog ? 28 : 18;
  const int targetY = animal == Animal::SunConure ? 99 : animal == Animal::Frog ? 89 : 82;
  pose.handX[hand] += (targetX - pose.handX[hand]) * ambient.groom / 256;
  pose.handY[hand] += (targetY - pose.handY[hand]) * ambient.groom / 256;
  pose.elbowX[hand] += (hand == 0 ? -5 : 5) * ambient.groom / 256;
  pose.headX += (hand == 0 ? -3 : 3) * ambient.groom / 256;
  pose.headY += 3 * ambient.groom / 256;
  pose.tilt += (hand == 0 ? -6 : 6) * ambient.groom / 256;
  pose.closed = ambient.groom > 128;
}

// Rendering and touch targets share interruption rules, so a pet remains
// tappable while roaming, grooming, or making an authored speech gesture.
struct PetMotion {
  ReactionPose reaction;
  AmbientPose ambient;
  PetMotion(const Snapshot& state, const Settings& settings, uint32_t ms,
            int action, int gesture, uint32_t gestureMs)
      : reaction(state.sleeping != 0, gesture, gestureMs),
        ambient(ambientPose(settings.animal, settings.scene, ms, state.sleeping != 0)) {
    if (reaction.id >= 0 || action != 0) {
      reaction.headX += ambient.headX; reaction.headY += ambient.headY;
      reaction.tilt += ambient.tilt;
      ambient.y = ambient.step = ambient.wing = ambient.travel = ambient.groom = 0;
    } else {
      applyAmbientPose(reaction, ambient, settings.animal);
    }
  }
};

// Rasterize the existing full-size head through a small affine tilt. Keeping
// this transform local preserves the species geometry and all HUD coordinates.
struct HeadCanvas {
  Canvas& canvas;
  int cx, cy, dx, dy, tilt;
  void dot(int x, int y, uint16_t color) {
    // Two integer shears are bijective on the pixel grid: no forward-mapping
    // holes appear in filled fur or faces as the head tilts.
    const int shearedX = x + (y - cy) * tilt / 32;
    const int tx = shearedX + dx;
    const int ty = y + dy - (shearedX - cx) * tilt / 32;
    if (ty >= 46 && ty <= 170) canvas.dot(tx, ty, color);
  }
  void oval(int x, int y, int rx, int ry, uint16_t color) {
    if (rx <= 0 || ry <= 0) return;
    for (int yy = -ry; yy <= ry; ++yy)
      for (int xx = -rx; xx <= rx; ++xx)
        if (xx * xx * ry * ry + yy * yy * rx * rx <= rx * rx * ry * ry)
          dot(x + xx, y + yy, color);
  }
  void line(int x0, int y0, int x1, int y1, uint16_t color, int radius = 0) {
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
      if (radius) oval(x0, y0, radius, radius, color); else dot(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      const int twice = 2 * error;
      if (twice >= dy) { error += dy; x0 += sx; }
      if (twice <= dx) { error += dx; y0 += sy; }
    }
  }
  void triangle(int ax, int ay, int bx, int by, int tx, int ty, uint16_t color) {
    const int left = ax < bx ? (ax < tx ? ax : tx) : (bx < tx ? bx : tx);
    const int right = ax > bx ? (ax > tx ? ax : tx) : (bx > tx ? bx : tx);
    const int top = ay < by ? (ay < ty ? ay : ty) : (by < ty ? by : ty);
    const int bottom = ay > by ? (ay > ty ? ay : ty) : (by > ty ? by : ty);
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
      const int a = (x - ax) * (by - ay) - (y - ay) * (bx - ax);
      const int b = (x - bx) * (ty - by) - (y - by) * (tx - bx);
      const int c = (x - tx) * (ay - ty) - (y - ty) * (ax - tx);
      if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) dot(x, y, color);
    }
  }
};

void slothBranch(Canvas& c) {
  c.line(47, 128, 193, 116, rgb(82, 63, 45), 5);
  c.line(48, 125, 194, 113, rgb(126, 95, 60), 3);
  c.line(56, 125, 88, 122, rgb(168, 129, 77));
  c.line(163, 116, 191, 114, rgb(168, 129, 77));
  c.line(180, 116, 192, 104, rgb(126, 95, 60), 2);
  leaf(c, 190, 107, false, mint, 8);
}

void reactionArms(Canvas& c, const ReactionPose& r, int x, int dy, Animal animal) {
  const bool frog = animal == Animal::Frog, cat = animal == Animal::Cat;
  const uint16_t coat = frog ? rgb(123, 177, 92) : cat ? rgb(207, 151, 99) : brown;
  const uint16_t shade = frog ? rgb(61, 117, 71) : cat ? rgb(150, 99, 68) : furDark;
  const uint16_t paw = frog ? rgb(219, 221, 152) : cat ? cream : furLight;
  for (int side = 0; side < 2; ++side) {
    const int sign = side == 0 ? -1 : 1;
    const int shoulderX = x + sign * (frog ? 24 : 28), shoulderY = 101 + dy;
    const int elbowX = x + r.elbowX[side], elbowY = r.elbowY[side] + dy;
    const int handX = x + r.handX[side], handY = r.handY[side] + dy;
    c.line(shoulderX, shoulderY, elbowX, elbowY, shade, frog ? 5 : 8);
    c.line(elbowX, elbowY, handX, handY, shade, frog ? 5 : 8);
    c.line(shoulderX, shoulderY - 2, elbowX, elbowY - 2, coat, frog ? 3 : 6);
    c.line(elbowX, elbowY - 2, handX, handY - 2, coat, frog ? 3 : 6);
    c.oval(handX, handY, frog ? 7 : cat ? 7 : 11, frog ? 4 : 5, paw);
    if (r.id == 9 && side == 1 && r.strength > 128) c.line(handX + 4, handY - 1, handX + 15, handY - 3, paw, 2);
    for (int finger = 0; finger < 3; ++finger) {
      if (frog) c.oval(handX - 6 + finger * 6, handY - 4, 3, 3, paw);
      else if (!cat) {
        const int start = 1 - 3 * r.strength / 256;
        const int end = 9 - 17 * r.strength / 256;
        c.line(handX - 6 + finger * 5, handY + start,
               handX - 5 + finger * 5, handY + end, cream);
      }
    }
  }
  if (r.id == 10 && r.strength > 128) heart(c, x, 105 + dy, rgb(234, 157, 132));
}

void drawSloth(Canvas& c, const Snapshot& state, uint32_t animationMs,
               int actionAnimation, const ReactionPose& reaction, const AmbientPose& ambient) {
  const bool asleep = state.sleeping != 0;
  const bool dancing = actionAnimation == 3 && !asleep && reaction.id < 0;
  const int bob = static_cast<int>((animationMs / 700) % 4);
  const unsigned danceStep = (animationMs / 70) % 12;
  const int danceSway[] = {-12, -10, -6, 0, 6, 10, 12, 10, 6, 0, -6, -10};
  const int danceBounce[] = {0, 2, 5, 2, 0, 2, 5, 2, 0, 2, 5, 2};
  const bool leftArmUp = danceStep < 6;
  const int dy = dancing ? danceBounce[danceStep]
                        : ((bob == 1 || bob == 2) ? 1 : 0) + reaction.bodyY + ambient.y;
  const bool blink = asleep || reaction.closed || (animationMs % 4700) > 4490;
  const int sway = dancing ? danceSway[danceStep]
      : (actionAnimation == 2 ? static_cast<int>((animationMs / 180) % 3) - 1 : 0);
  const int cx = 120 + sway + reaction.bodyX + ambient.x;

  if (dancing) {
    // The branch stays put while Moss steps from side to side above it.
    c.line(47, 135, 193, 128, rgb(82, 63, 45), 5);
    c.line(48, 132, 194, 125, rgb(126, 95, 60), 3);
    c.line(57, 132, 180, 126, rgb(168, 129, 77));
    for (unsigned i = 0; i < 9; ++i) {
      const int fall = static_cast<int>((animationMs / 15 + i * 19) % 84);
      const int flutter = static_cast<int>((animationMs / 110 + i * 3) % 7) - 3;
      const int x = 43 + static_cast<int>(i) * 19 + flutter;
      leaf(c, x, 58 + fall, ((animationMs / 180 + i) % 2) != 0,
           i % 3 == 0 ? gold : mint, 4 + i % 2);
    }
  }

  // Pear-shaped body, little feet, then the broad round sloth head.
  if (!dancing) c.oval(cx, 130 + dy, 37, 9, rgb(10, 30, 30));
  c.oval(cx, 111 + dy, 35, 26, furDark);
  c.oval(cx, 106 + dy, 33, 26, brown);
  c.oval(cx + 1, 113 + dy, 24, 20, furLight);
  c.oval(cx - 6, 111 + dy, 12, 15, rgb(192, 156, 116));
  for (int tuft = -1; tuft <= 1; ++tuft) {
    c.line(cx + tuft * 8 - 3, 114 + dy, cx + tuft * 8, 117 + dy, brown);
    c.line(cx + tuft * 8, 117 + dy, cx + tuft * 8 + 3, 114 + dy, brown);
  }
  const int leftFootY = 128 + dy - (dancing && !leftArmUp ? 11 : 0) + ambient.step;
  const int rightFootY = 128 + dy - (dancing && leftArmUp ? 11 : 0) - ambient.step;
  const int footSpread = dancing ? 5 : 0;
  c.oval(cx - 24 - footSpread, leftFootY + 2, 14, 7, furDark);
  c.oval(cx + 24 + footSpread, rightFootY + 2, 14, 7, furDark);
  c.oval(cx - 24 - footSpread, leftFootY, 13, 7, brown);
  c.oval(cx + 24 + footSpread, rightFootY, 13, 7, brown);
  if (dancing) {
    for (int toe = 0; toe < 3; ++toe) {
      c.line(cx - 34 + toe * 4, leftFootY + 1,
             cx - 34 + toe * 4, leftFootY + 6, cream);
      c.line(cx + 25 + toe * 4, rightFootY + 1,
             cx + 25 + toe * 4, rightFootY + 6, cream);
    }
  }
  HeadCanvas head{c, cx, 80 + dy, reaction.headX, reaction.headY, reaction.tilt};
  head.oval(cx, 81 + dy, 38, 31, furDark);
  head.oval(cx, 78 + dy, 38, 30, brown);
  // Small tufts suggest fur without disguising the head silhouette.
  head.line(cx - 15, 51 + dy, cx - 19, 47 + dy, brown, 1);
  head.line(cx - 7, 50 + dy, cx - 8, 46 + dy, brown, 1);
  head.line(cx + 20, 55 + dy, cx + 24, 51 + dy, brown, 1);
  head.oval(cx, 80 + dy, 29, 24, face);
  head.oval(cx - 11, 76 + dy, 18, 16, cream);
  head.oval(cx + 11, 76 + dy, 18, 16, cream);
  // Characteristic downward-slanting eye masks; small eyes nested inside.
  head.line(cx - 10, 75 + dy, cx - 23, 83 + dy, mask, 6);
  head.line(cx + 10, 75 + dy, cx + 23, 83 + dy, mask, 6);
  if (dancing || reaction.happy) {
    head.line(cx - 20, 79 + dy, cx - 16, 76 + dy, ink, 1);
    head.line(cx - 16, 76 + dy, cx - 12, 79 + dy, ink, 1);
    head.line(cx + 12, 79 + dy, cx + 16, 76 + dy, ink, 1);
    head.line(cx + 16, 76 + dy, cx + 20, 79 + dy, ink, 1);
  } else if (blink) {
    head.line(cx - 20, 78 + dy, cx - 16, 80 + dy, ink, 1);
    head.line(cx - 16, 80 + dy, cx - 12, 77 + dy, ink, 1);
    head.line(cx + 12, 77 + dy, cx + 16, 80 + dy, ink, 1);
    head.line(cx + 16, 80 + dy, cx + 20, 78 + dy, ink, 1);
  } else {
    head.oval(cx - 16 + ambient.gaze, 77 + dy, 3, 4, ink);
    head.oval(cx + 16 + ambient.gaze, 77 + dy, 3, 4, ink);
    head.dot(cx - 17 + ambient.gaze, 75 + dy, cream);
    head.dot(cx + 15 + ambient.gaze, 75 + dy, cream);
  }
  head.oval(cx, 86 + dy, 5, 3, mask);
  head.line(cx, 88 + dy, cx, 91 + dy, mask);
  head.line(cx - 7, 91 + dy, cx - 4, 94 + dy, mask);
  head.line(cx - 4, 94 + dy, cx + 4, 94 + dy, mask);
  head.line(cx + 4, 94 + dy, cx + 7, 91 + dy, mask);
  if (dancing || reaction.openMouth) {
    head.oval(cx, 94 + dy, 7, 5, mask);
    head.oval(cx, 96 + dy, 4, 2, rgb(224, 160, 125));
  }
  head.oval(cx - 24, 89 + dy, 3, 2, rgb(222, 166, 135));
  head.oval(cx + 24, 89 + dy, 3, 2, rgb(222, 166, 135));

  if (reaction.id == 0 && reaction.strength > 128) head.line(cx + 11, 68 + dy, cx + 22, 65 + dy, mask, 1);

  if (dancing) {
    // Alternate overhead and outstretched hands, with matching high steps.
    for (int side = -1; side <= 1; side += 2) {
      const bool raised = side < 0 ? leftArmUp : !leftArmUp;
      const int handX = cx + side * 53;
      const int handY = (raised ? 69 : 104) + dy;
      const int elbowY = (raised ? 91 : 114) + dy;
      c.line(cx + side * 29, 100 + dy, cx + side * 43, elbowY, furDark, 8);
      c.line(cx + side * 43, elbowY, handX, handY, furDark, 8);
      c.line(cx + side * 29, 98 + dy, cx + side * 43, elbowY - 2, brown, 6);
      c.line(cx + side * 43, elbowY - 2, handX, handY, brown, 6);
      c.oval(handX, handY, 8, 6, furLight);
      for (int finger = 0; finger < 3; ++finger) {
        c.line(handX - 5 + finger * 5, handY - 2,
               handX - 6 + finger * 5, handY - 10, cream, 1);
      }
    }
    // Two foreground leaves flutter beside the hands, never over the meters.
    for (unsigned i = 0; i < 2; ++i) {
      const int fall = static_cast<int>((animationMs / 11 + i * 43) % 76);
      leaf(c, i == 0 ? 34 : 204, 65 + fall, i == 0, gold, 5);
    }
  } else if (reaction.id >= 0) {
    slothBranch(c);
    reactionArms(c, reaction, cx, dy, Animal::Sloth);
  } else {
    // Long relaxed arms curl around a branch; three ivory claws on each hand.
    c.line(cx - 28, 97 + dy, cx - 34, 114 + dy, furDark, 9);
    c.line(cx - 34, 114 + dy, cx - 20, 121 + dy, furDark, 9);
    c.line(cx + 28, 97 + dy, cx + 34, 114 + dy, furDark, 9);
    c.line(cx + 34, 114 + dy, cx + 20, 121 + dy, furDark, 9);
    c.line(cx - 29, 97 + dy, cx - 34, 112 + dy, brown, 7);
    c.line(cx - 34, 112 + dy, cx - 20, 119 + dy, brown, 7);
    c.line(cx + 29, 97 + dy, cx + 34, 112 + dy, brown, 7);
    c.line(cx + 34, 112 + dy, cx + 20, 119 + dy, brown, 7);
    slothBranch(c);
    c.oval(cx - 21, 119 + dy, 11, 5, furLight);
    c.oval(cx + 21, 116 + dy, 11, 5, furLight);
    for (int finger = 0; finger < 3; ++finger) {
      c.line(cx - 27 + finger * 5, 120 + dy,
             cx - 26 + finger * 5, 128 + dy, cream, 1);
      c.line(cx + 15 + finger * 5, 117 + dy,
             cx + 16 + finger * 5, 125 + dy, cream, 1);
    }
  }

  if (asleep) {
    c.text(163, 66 - dy * 2, "Z", gold);
    c.text(175, 52 - dy * 2, "Z", gold, 2);
  } else if (actionAnimation == 1) {
    leaf(c, cx + 1, 108 - ((animationMs / 220) % 3), false, mint, 8);
    c.dot(cx - 7, 100, cream);
    c.dot(cx + 15, 98, cream);
  } else if (actionAnimation == 2) {
    heart(c, 65, 76 - ((animationMs / 150) % 5), rgb(234, 157, 132));
    heart(c, 177, 89 - ((animationMs / 210) % 5), gold);
  }

}

void flower(Canvas& c, int x, int y, uint16_t petals) {
  c.line(x, y, x, y + 12, rgb(75, 132, 83));
  c.oval(x - 3, y, 3, 2, petals);
  c.oval(x + 3, y, 3, 2, petals);
  c.oval(x, y - 3, 2, 3, petals);
  c.oval(x, y + 3, 2, 3, petals);
  c.oval(x, y, 2, 2, gold);
}

void windowGrid(Canvas& c, int x, int y, int columns, int rows, uint16_t color) {
  for (int row = 0; row < rows; ++row)
    for (int column = 0; column < columns; ++column)
      if ((row + column * 2) % 4 != 0)
        c.rect(x + column * 5, y + row * 7, 2, 3, color);
}

void drawCity(Canvas& c) {
  const uint16_t towers = rgb(43, 52, 73), near = rgb(31, 40, 57);
  const uint16_t windows = rgb(194, 158, 96);
  c.oval(181, 58, 7, 7, rgb(233, 205, 152));
  c.rect(13, 105, 22, 39, towers);
  c.rect(17, 99, 14, 6, towers);
  windowGrid(c, 17, 110, 4, 4, windows);
  // Stepped Art Deco crown and mast evoke the Empire State Building.
  c.rect(36, 89, 25, 55, near);
  c.rect(39, 78, 19, 12, towers);
  c.rect(43, 65, 11, 14, towers);
  c.rect(46, 58, 5, 8, towers);
  c.line(48, 46, 48, 59, windows);
  windowGrid(c, 41, 92, 4, 7, windows);
  windowGrid(c, 45, 70, 2, 3, windows);
  c.rect(64, 119, 20, 25, towers);
  windowGrid(c, 68, 124, 3, 3, windows);
  // A second narrow tower, rooftop water tank, and a yellow street taxi.
  c.rect(188, 76, 22, 68, towers);
  c.triangle(188, 76, 199, 55, 210, 76, towers);
  c.line(199, 45, 199, 57, windows);
  windowGrid(c, 192, 82, 3, 8, windows);
  c.rect(213, 112, 14, 32, near);
  c.line(217, 111, 217, 99, towers);
  c.line(224, 111, 224, 99, towers);
  c.rect(214, 92, 13, 12, towers);
  c.triangle(213, 92, 220, 87, 228, 92, towers);
  c.line(13, 144, 227, 144, rgb(90, 91, 91));
  c.roundRect(23, 134, 25, 8, 2, gold);
  c.rect(29, 130, 13, 6, gold);
  c.rect(31, 131, 9, 4, rgb(71, 104, 121));
  c.oval(28, 142, 3, 3, ink);
  c.oval(43, 142, 3, 3, ink);
  c.rect(185, 137, 18, 2, windows);
}

void star(Canvas& c, int x, int y, uint16_t color) {
  c.line(x - 2, y, x + 2, y, color);
  c.line(x, y - 2, x, y + 2, color);
}

void drawSpace(Canvas& c) {
  const uint16_t purple = rgb(167, 139, 184), orbit = rgb(121, 120, 167);
  const int stars[][2] = {{23,55},{63,71},{163,47},{225,111},{194,137},{69,134}};
  for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i)
    star(c, stars[i][0], stars[i][1], i % 2 ? muted : cream);
  for (unsigned i = 0; i < 17; ++i)
    c.dot(15 + int(i * 37 % 213), 47 + int(i * 23 % 96), orbit);
  // Saturn's ring is drawn behind the globe, then in front across its lower arc.
  c.oval(197, 66, 24, 7, orbit);
  c.oval(197, 65, 21, 4, rgb(18, 22, 47));
  c.oval(197, 62, 12, 12, purple);
  c.line(188, 57, 205, 57, rgb(199, 171, 191));
  c.line(186, 62, 208, 62, rgb(199, 171, 191));
  c.line(175, 68, 184, 71, orbit, 1);
  c.line(184, 71, 208, 71, orbit, 1);
  c.line(208, 71, 219, 68, orbit, 1);
  // A little blue world and a tiny launch vehicle distinguish this from Night.
  c.oval(34, 90, 13, 13, rgb(70, 133, 173));
  c.oval(29, 85, 5, 4, rgb(126, 179, 151));
  c.oval(39, 95, 5, 6, rgb(126, 179, 151));
  c.rect(29, 115, 9, 18, cream);
  c.triangle(29, 115, 33, 107, 38, 115, rgb(225, 152, 127));
  c.triangle(29, 124, 24, 134, 29, 132, rgb(225, 152, 127));
  c.triangle(38, 124, 43, 134, 38, 132, rgb(225, 152, 127));
  c.oval(33, 120, 3, 3, rgb(70, 133, 173));
  c.triangle(30, 133, 36, 133, 33, 143, gold);
  c.rect(196, 111, 7, 8, cream);
  c.rect(183, 111, 10, 8, rgb(73, 116, 162));
  c.rect(206, 111, 10, 8, rgb(73, 116, 162));
  c.line(189, 115, 211, 115, muted);
  c.line(199, 109, 204, 104, muted);
}

void palm(Canvas& c, int x, int y, int height) {
  const int topX = x + 5, topY = y - height;
  const uint16_t trunk = rgb(157, 125, 77), frond = rgb(78, 157, 109);
  c.line(x, y, x + 2, y - height / 2, trunk, 2);
  c.line(x + 2, y - height / 2, topX, topY, trunk, 2);
  for (int mark = 5; mark < height; mark += 8)
    c.line(x - 1 + mark / 12, y - mark, x + 3 + mark / 12, y - mark - 1,
           rgb(202, 162, 103));
  const int tips[][2] = {{-24,4},{-16,-10},{-1,-14},{17,-10},{24,5},{-19,14},{17,15}};
  for (unsigned i = 0; i < sizeof(tips) / sizeof(tips[0]); ++i) {
    const int midX = topX + tips[i][0] / 2;
    const int midY = topY + tips[i][1] / 2 - 4;
    c.line(topX, topY, midX, midY, frond, 2);
    c.line(midX, midY, topX + tips[i][0], topY + tips[i][1], frond, 1);
  }
  c.oval(topX - 3, topY + 3, 3, 3, trunk);
  c.oval(topX + 3, topY + 4, 3, 3, trunk);
}

void drawIsland(Canvas& c) {
  c.oval(198, 62, 12, 12, rgb(240, 181, 105));
  c.line(176, 62, 220, 62, rgb(226, 156, 93));
  c.oval(120, 128, 108, 18, rgb(36, 104, 115));
  c.line(18, 114, 222, 114, rgb(81, 151, 150));
  c.line(175, 126, 215, 126, rgb(113, 181, 167));
  c.line(169, 134, 195, 134, rgb(113, 181, 167));
  c.line(21, 120, 62, 120, rgb(113, 181, 167));
  c.oval(46, 138, 34, 8, rgb(194, 172, 116));
  c.oval(199, 139, 23, 6, rgb(194, 172, 116));
  palm(c, 39, 136, 51);
  palm(c, 199, 138, 32);
  c.line(62, 55, 67, 53, cream);
  c.line(67, 53, 72, 55, cream);
  c.line(169, 87, 173, 85, muted);
  c.line(173, 85, 177, 87, muted);
  c.oval(24, 139, 3, 2, cream);
  c.dot(58, 140, cream);
}

void bubble(Canvas& c, int x, int y, int radius) {
  c.oval(x, y, radius, radius, rgb(115, 181, 186));
  c.oval(x, y, radius - 1, radius - 1, rgb(19, 60, 75));
  c.dot(x - radius + 1, y - 1, cream);
}

void reefFish(Canvas& c, int x, int y, bool right, uint16_t color) {
  const int side = right ? 1 : -1;
  c.oval(x, y, 7, 4, color);
  c.triangle(x - side * 5, y, x - side * 12, y - 5,
             x - side * 12, y + 5, color);
  c.line(x - 1, y - 3, x - 1, y + 3, cream);
  c.dot(x + side * 4, y - 1, ink);
}

void drawUnderSea(Canvas& c) {
  c.triangle(40, 44, 20, 127, 66, 124, rgb(22, 61, 73));
  c.triangle(182, 44, 171, 123, 213, 126, rgb(22, 61, 73));
  c.oval(120, 143, 108, 5, rgb(75, 98, 91));
  bubble(c, 27, 58, 4);
  bubble(c, 43, 67, 2);
  bubble(c, 211, 58, 5);
  bubble(c, 201, 74, 3);
  bubble(c, 219, 98, 2);
  reefFish(c, 40, 85, true, rgb(227, 160, 111));
  reefFish(c, 196, 92, false, rgb(138, 168, 202));
  reefFish(c, 213, 113, false, rgb(227, 186, 116));
  const uint16_t coral = rgb(186, 112, 129), seaweed = rgb(72, 148, 125);
  c.line(38, 142, 38, 117, coral, 2);
  c.line(38, 128, 28, 123, coral, 2);
  c.line(28, 123, 27, 112, coral, 2);
  c.line(38, 134, 49, 129, coral, 2);
  c.line(49, 129, 50, 119, coral, 2);
  c.line(38, 124, 43, 118, coral, 1);
  c.line(19, 143, 22, 131, seaweed, 2);
  c.line(22, 131, 17, 119, seaweed, 2);
  c.line(17, 119, 20, 109, seaweed, 2);
  c.line(209, 142, 205, 129, seaweed, 2);
  c.line(205, 129, 211, 119, seaweed, 2);
  c.line(220, 143, 220, 128, seaweed, 2);
  c.line(220, 128, 225, 119, seaweed, 2);
  c.oval(185, 142, 11, 4, rgb(145, 127, 165));
  c.line(181, 140, 178, 133, rgb(181, 151, 187), 1);
  c.line(185, 140, 185, 131, rgb(181, 151, 187), 1);
  c.line(189, 140, 194, 134, rgb(181, 151, 187), 1);
  c.dot(63, 145, cream); c.dot(174, 145, cream);
}

void drawScene(Canvas& c, Scene scene) {
  const bool meadow = scene == Scene::Meadow;
  const bool night = scene == Scene::Night;
  const bool city = scene == Scene::NYC;
  const bool space = scene == Scene::Space;
  const bool island = scene == Scene::Island;
  const bool underSea = scene == Scene::UnderSea;
  for (int y = 0; y < kSize; ++y) {
    const uint16_t bg = space ? rgb(16 + y / 90, 19 + y / 90, 42 + y / 60)
        : city ? rgb(18 + y / 90, 28 + y / 90, 45 + y / 60)
        : island ? rgb(12 + y / 90, 43 + y / 70, 54 + y / 80)
        : underSea ? rgb(11 + y / 90, 36 + y / 70, 52 + y / 80)
        : night ? rgb(11 + y / 90, 21 + y / 60, 41 + y / 80)
        : meadow ? rgb(18 + y / 80, 44 + y / 60, 43 + y / 80)
        : rgb(10 + y * 4 / 240, 34 + y * 8 / 240, 35 + y * 3 / 240);
    c.rect(0, y, kSize, 1, bg);
  }
  c.oval(120, 97, 69, 46, space ? rgb(28, 31, 58)
      : city ? rgb(30, 42, 59) : island ? rgb(27, 75, 82)
      : underSea ? rgb(20, 66, 79) : night ? rgb(24, 38, 61)
      : meadow ? rgb(37, 73, 61) : rgb(22, 54, 48));
  if (city) {
    drawCity(c);
  } else if (space) {
    drawSpace(c);
  } else if (island) {
    drawIsland(c);
  } else if (underSea) {
    drawUnderSea(c);
  } else if (meadow) {
    c.oval(46, 130, 35, 13, rgb(40, 80, 59));
    c.oval(196, 132, 32, 11, rgb(42, 85, 61));
    c.oval(203, 57, 9, 9, gold);
    c.line(203, 44, 203, 46, muted);
    c.line(216, 57, 218, 57, muted);
    c.line(190, 57, 192, 57, muted);
    flower(c, 28, 112, rgb(229, 165, 152));
    flower(c, 44, 126, cream);
    flower(c, 207, 109, cream);
    flower(c, 193, 129, rgb(229, 165, 152));
    c.line(35, 69, 40, 67, muted);
    c.line(40, 67, 45, 69, muted);
  } else if (night) {
    c.oval(202, 59, 11, 11, cream);
    c.oval(207, 55, 10, 10, rgb(11, 21, 41));
    const int stars[][2] = {{28,60},{48,92},{73,49},{171,48},{216,93},{25,131},{190,121}};
    for (unsigned i = 0; i < sizeof(stars) / sizeof(stars[0]); ++i) {
      c.line(stars[i][0] - 2, stars[i][1], stars[i][0] + 2, stars[i][1], muted);
      c.line(stars[i][0], stars[i][1] - 2, stars[i][0], stars[i][1] + 2, muted);
    }
    c.line(16, 143, 34, 100, rgb(53, 74, 96), 1);
    leaf(c, 24, 127, false, rgb(40, 68, 91), 11);
    c.line(221, 143, 210, 111, rgb(53, 74, 96), 1);
    leaf(c, 219, 137, true, rgb(40, 68, 91), 10);
  } else {
    c.line(18, 142, 30, 83, rgb(48, 90, 67), 1);
    leaf(c, 26, 102, true, rgb(35, 76, 58), 12);
    leaf(c, 22, 122, false, rgb(49, 99, 70), 10);
    leaf(c, 18, 141, true, rgb(39, 85, 64), 13);
    c.line(220, 142, 209, 73, rgb(48, 90, 67), 1);
    leaf(c, 211, 96, false, rgb(43, 87, 63), 13);
    leaf(c, 215, 117, true, rgb(52, 102, 74), 11);
    leaf(c, 219, 137, false, rgb(37, 82, 62), 12);
    c.dot(48, 53, muted); c.dot(185, 43, muted); c.dot(199, 68, muted);
    c.line(41, 70, 45, 70, rgb(98, 138, 103));
    c.line(43, 68, 43, 72, rgb(98, 138, 103));
  }
}

struct Pose {
  int x, dy;
  bool asleep, blink, dancing, leftUp;
  Pose(const Snapshot& state, uint32_t ms, int action) {
    asleep = state.sleeping != 0;
    dancing = action == 3 && !asleep;
    blink = asleep || ms % 4700 > 4490;
    const unsigned step = (ms / 70) % 12;
    const int sway[] = {-12,-10,-6,0,6,10,12,10,6,0,-6,-10};
    const int bounce[] = {0,2,5,2,0,2,5,2,0,2,5,2};
    x = 120 + (dancing ? sway[step] : action == 2 ? int(ms / 180 % 3) - 1 : 0);
    dy = dancing ? bounce[step] : (ms / 700 % 4 == 1 || ms / 700 % 4 == 2 ? 1 : 0);
    leftUp = step < 6;
  }
};

template <typename Paint>
void smallEyes(Paint& c, int x, int y, int spread, const Pose& p) {
  for (int side = -1; side <= 1; side += 2) {
    const int eye = x + side * spread;
    if (p.blink || p.dancing) {
      const int bend = p.dancing ? -2 : 2;
      c.line(eye - 4, y, eye, y + bend, ink, 1);
      c.line(eye, y + bend, eye + 4, y, ink, 1);
    } else {
      c.oval(eye, y, 3, 4, ink);
      c.dot(eye - 1, y - 2, cream);
    }
  }
}

void otherPetEffects(Canvas& c, const Pose& p, uint32_t ms, int action) {
  if (p.asleep) {
    c.text(168, 63 - p.dy * 2, "Z", gold);
    c.text(181, 49 - p.dy * 2, "Z", gold, 2);
  } else if (action == 2) {
    heart(c, 64, 76 - int(ms / 150 % 5), rgb(234, 157, 132));
    heart(c, 180, 86 - int(ms / 210 % 5), gold);
  } else if (p.dancing) {
    for (unsigned i = 0; i < 6; ++i) {
      const int x = (i < 3 ? 33 : 181) + int(i % 3) * 12;
      const int y = 60 + int((ms / 14 + i * 21) % 79);
      leaf(c, x, y, (i + ms / 180) % 2, i % 2 ? mint : gold, 4);
    }
  }
}

void drawCat(Canvas& c, const Snapshot& state, uint32_t ms, int action, const ReactionPose& reaction, const AmbientPose& ambient) {
  Pose p(state, ms, reaction.id >= 0 ? 0 : action);
  p.x += reaction.bodyX + ambient.x; p.dy += reaction.bodyY + ambient.y;
  p.blink = p.blink || reaction.closed;
  const int x = p.x, y = p.dy;
  const uint16_t coat = rgb(207, 151, 99), shade = rgb(150, 99, 68);
  const uint16_t pink = rgb(226, 154, 147);
  // Curled tail, tall pointed ears, whiskers, tabby marks, and soft white paws.
  c.oval(x, 139, 42, 5, rgb(13, 33, 32));
  c.line(x + 23, 125 + y, x + 41, 119 + y, shade, 6);
  c.line(x + 41, 119 + y, x + 47 + ambient.tail, 103 + y, shade, 6);
  c.line(x + 47 + ambient.tail, 103 + y, x + 41 + ambient.tail, 97 + y, coat, 5);
  c.oval(x, 113 + y, 29, 25, shade);
  c.oval(x, 110 + y, 28, 25, coat);
  c.oval(x, 113 + y, 17, 20, cream);
  c.oval(x - 4, 110 + y, 10, 14, rgb(255, 244, 216));
  for (int side = -1; side <= 1; side += 2) {
    c.line(x + side * 22, 108 + y, x + side * 28, 111 + y, shade, 1);
    c.line(x + side * 23, 116 + y, x + side * 28, 118 + y, shade, 1);
  }
  c.line(x + 35, 119 + y, x + 39, 124 + y, coat, 2);
  for (int side = -1; side <= 1; side += 2) {
    const int footY = 131 + y - (p.dancing && (side < 0 ? !p.leftUp : p.leftUp) ? 11 : 0) + side * ambient.step;
    c.oval(x + side * 24, footY, 13, 7, coat);
    c.oval(x + side * 26, footY + 1, 10, 5, cream);
    if (reaction.id < 0) {
      const bool up = p.dancing && (side < 0 ? p.leftUp : !p.leftUp);
      const int handX = x + side * (p.dancing ? 45 : 21);
      const int handY = up ? 76 + y : 119 + y;
      c.line(x + side * 23, 103 + y, handX, handY, shade, 7);
      c.line(x + side * 23, 101 + y, handX, handY - 2, coat, 5);
      c.oval(handX, handY - 3, 7, 6, cream);
    }
  }
  HeadCanvas head{c, x, 80 + y, reaction.headX, reaction.headY, reaction.tilt};
  Pose eyes = p; eyes.dancing = eyes.dancing || reaction.happy;
  head.triangle(x - 31, 70 + y, x - 27, 45 + y, x - 9, 61 + y, shade);
  head.triangle(x + 31, 70 + y, x + 27, 45 + y, x + 9, 61 + y, shade);
  head.triangle(x - 27, 66 + y, x - 25, 51 + y, x - 15, 63 + y, pink);
  head.triangle(x + 27, 66 + y, x + 25, 51 + y, x + 15, 63 + y, pink);
  head.oval(x, 82 + y, 33, 26, shade);
  head.oval(x, 79 + y, 33, 25, coat);
  head.line(x, 56 + y, x, 65 + y, shade, 2);
  head.line(x - 9, 57 + y, x - 6, 65 + y, shade, 1);
  head.line(x + 9, 57 + y, x + 6, 65 + y, shade, 1);
  head.oval(x - 7, 90 + y, 10, 7, cream);
  head.oval(x + 7, 90 + y, 10, 7, cream);
  smallEyes(head, x + ambient.gaze, 77 + y, 15, eyes);
  head.triangle(x - 4, 85 + y, x + 4, 85 + y, x, 89 + y, pink);
  head.line(x, 89 + y, x - 4, 93 + y, ink);
  head.line(x, 89 + y, x + 4, 93 + y, ink);
  if (p.dancing || reaction.openMouth) head.oval(x, 94 + y, 4, 3, pink);
  for (int side = -1; side <= 1; side += 2) {
    head.line(x + side * 21, 87 + y, x + side * 37, 84 + y, cream);
    head.line(x + side * 21, 91 + y, x + side * 36, 93 + y, cream);
  }
  if (reaction.id >= 0) reactionArms(c, reaction, x, y, Animal::Cat);
  if (!p.asleep && action == 1) {
    const int snackY = 107 - int(ms / 220 % 3);
    c.oval(x + 1, snackY, 9, 4, rgb(150, 201, 211));
    c.triangle(x - 6, snackY, x - 13, snackY - 5, x - 13, snackY + 5, mint);
    c.dot(x + 6, snackY - 1, ink);
  }
  otherPetEffects(c, p, ms, action);
}

void drawFrog(Canvas& c, const Snapshot& state, uint32_t ms, int action, const ReactionPose& reaction, const AmbientPose& ambient) {
  Pose p(state, ms, reaction.id >= 0 ? 0 : action);
  p.x += reaction.bodyX + ambient.x; p.dy += reaction.bodyY + ambient.y;
  p.blink = p.blink || reaction.closed;
  const int x = p.x, y = p.dy;
  const uint16_t green = rgb(123, 177, 92), shade = rgb(61, 117, 71);
  const uint16_t belly = rgb(219, 221, 152), pink = rgb(218, 157, 129);
  c.oval(120, 137, 49, 8, rgb(46, 104, 73));
  c.line(120, 137, 151, 141, rgb(94, 154, 104));
  c.oval(x, 114 + y, 29, 23, shade);
  c.oval(x, 111 + y, 28, 23, green);
  c.oval(x, 115 + y, 19, 17, belly);
  const int breath = !p.asleep && action == 0 && reaction.id < 0 ? int(ms / 420 % 4) / 2 : 0;
  c.oval(x, 107 + y, 13 + breath, 8 + breath, rgb(233, 231, 171));
  for (int side = -1; side <= 1; side += 2) {
    c.oval(x + side * 25, 109 + y, 3, 2, rgb(92, 147, 70));
    c.oval(x + side * 21, 119 + y, 2, 2, rgb(92, 147, 70));
  }
  for (int side = -1; side <= 1; side += 2) {
    const int footY = 131 + y - (p.dancing && (side < 0 ? !p.leftUp : p.leftUp) ? 10 : 0) + (ambient.activity == AmbientActivity::Travel ? 3 : 0);
    c.oval(x + side * 26, footY - 7, 16, 12, shade);
    c.oval(x + side * 29, footY, 17, 6, green);
    for (int toe = 0; toe < 3; ++toe)
      c.oval(x + side * (20 + toe * 8), footY + 2, 4, 3, belly);
    if (reaction.id < 0) {
      const bool up = p.dancing && (side < 0 ? p.leftUp : !p.leftUp);
      const int handX = x + side * (p.dancing ? 48 : 25);
      const int handY = up ? 77 + y : 118 + y;
      c.line(x + side * 24, 101 + y, handX, handY, shade, 4);
      c.line(x + side * 24, 100 + y, handX, handY - 2, green, 3);
      for (int finger = 0; finger < 3; ++finger)
        c.oval(handX - 5 + finger * 5, handY - 2 - (up ? 3 : 0), 3, 3, belly);
    }
  }
  HeadCanvas head{c, x, 80 + y, reaction.headX, reaction.headY, reaction.tilt};
  Pose eyes = p; eyes.dancing = eyes.dancing || reaction.happy;
  head.oval(x, 86 + y, 39, 24, shade);
  head.oval(x, 82 + y, 39, 23, green);
  head.line(x - 13, 65 + y, x + 11, 65 + y, rgb(150, 196, 110), 1);
  head.oval(x - 25, 63 + y, 12, 15, shade);
  head.oval(x + 25, 63 + y, 12, 15, shade);
  head.oval(x - 25, 61 + y, 11, 13, green);
  head.oval(x + 25, 61 + y, 11, 13, green);
  head.oval(x - 25, 61 + y, 7, 9, cream);
  head.oval(x + 25, 61 + y, 7, 9, cream);
  smallEyes(head, x + ambient.gaze, 61 + y, 25, eyes);
  head.dot(x - 6, 80 + y, shade); head.dot(x + 6, 80 + y, shade);
  head.line(x - 21, 88 + y, x - 13, 94 + y, shade);
  head.line(x - 13, 94 + y, x + 13, 94 + y, shade);
  head.line(x + 13, 94 + y, x + 21, 88 + y, shade);
  head.oval(x - 28, 85 + y, 4, 3, pink);
  head.oval(x + 28, 85 + y, 4, 3, pink);
  if (p.dancing || reaction.openMouth) {
    head.oval(x, 93 + y, 10, 6, shade);
    head.oval(x, 96 + y, 6, 2, pink);
  }
  if (reaction.id >= 0) reactionArms(c, reaction, x, y, Animal::Frog);
  if (!p.asleep && action == 1) {
    const int bugX = x + 34 + int(ms / 170 % 5);
    c.line(x + 8, 93 + y, bugX - 3, 97 + y, pink, 1);
    c.oval(bugX - 2, 92 + y, 3, 4, cream);
    c.oval(bugX + 3, 92 + y, 3, 4, cream);
    c.oval(bugX, 97 + y, 4, 3, gold);
    c.line(bugX, 95 + y, bugX, 99 + y, ink);
  }
  otherPetEffects(c, p, ms, action);
}

void conurePerch(Canvas& c, Scene scene) {
  if (scene == Scene::NYC) {
    c.roundRect(56, 137, 128, 7, 2, rgb(81, 104, 123));
    c.line(59, 137, 180, 137, rgb(151, 171, 181));
  } else if (scene == Scene::Space) {
    c.oval(120, 142, 58, 5, rgb(79, 102, 141));
    c.line(73, 139, 167, 139, rgb(130, 193, 209));
    for (int x = 90; x <= 150; x += 15) c.dot(x, 143, gold);
  } else {
    const uint16_t branch = scene == Scene::UnderSea ? rgb(178, 116, 122) : rgb(120, 91, 58);
    c.line(49, 139, 192, 139, branch, 4);
    c.line(52, 137, 190, 137, scene == Scene::UnderSea ? rgb(221, 162, 148) : rgb(176, 137, 83));
    c.line(177, 139, 189, 128, branch, 2);
    if (scene != Scene::UnderSea) leaf(c, 186, 130, false, mint, 6);
  }
}

void conureWings(Canvas& c, const ReactionPose& r, const AmbientPose& ambient,
                  const Pose& p) {
  const uint16_t green = rgb(77, 150, 70), edge = rgb(45, 97, 57);
  const uint16_t blue = rgb(58, 111, 164), lime = rgb(178, 197, 76);
  for (int side = 0; side < 2; ++side) {
    const int sign = side == 0 ? -1 : 1;
    int elbowX = p.x + sign * 27, elbowY = 116 + p.dy;
    int tipX = p.x + sign * 20, tipY = 130 + p.dy;
    if (r.id >= 0) {
      elbowX = p.x + r.elbowX[side]; elbowY = r.elbowY[side] + p.dy;
      tipX = p.x + r.handX[side]; tipY = r.handY[side] + p.dy;
      // Fold smoothly back into an avian wing, rather than the mammal hand's
      // home position used by the shared gesture skeleton.
      const int folded = 256 - r.strength;
      elbowX -= sign * 7 * folded / 256;
      elbowY += 4 * folded / 256;
      tipX -= sign * folded / 256;
      tipY += (side == 0 ? 11 : 14) * folded / 256;
    } else if (ambient.travel || p.dancing) {
      const int spread = p.dancing ? 17 : ambient.wing;
      const int amount = p.dancing ? 256 : ambient.travel;
      elbowX += sign * 7 * amount / 256; elbowY -= 16 * amount / 256;
      tipX += sign * (16 + spread) * amount / 256;
      const int targetY = p.dancing ? (side == 0 ? p.leftUp : !p.leftUp) ? 75 : 115 : 105 - spread * 2;
      tipY += (targetY - 130) * amount / 256;
    }
    c.line(p.x + sign * 21, 101 + p.dy, elbowX, elbowY, edge, 9);
    c.line(elbowX, elbowY, tipX, tipY, edge, 8);
    c.line(p.x + sign * 21, 99 + p.dy, elbowX, elbowY - 2, green, 7);
    c.line(elbowX, elbowY - 2, tipX, tipY - 1, green, 6);
    for (int feather = 0; feather < 3; ++feather) {
      c.line(tipX + sign * (feather - 1) * 3, tipY - 2,
             tipX + sign * (feather + 1) * 3, tipY + 7 + feather * 2, blue, 2);
    }
    c.line(p.x + sign * 20, 96 + p.dy, elbowX, elbowY - 5, lime, 2);
  }
  if (r.id == 10 && r.strength > 128) heart(c, p.x, 106 + p.dy, rgb(237, 137, 126));
}

void drawConure(Canvas& c, const Snapshot& state, uint32_t ms, int action,
                 const ReactionPose& reaction, const AmbientPose& ambient, Scene scene) {
  Pose p(state, ms, reaction.id >= 0 ? 0 : action);
  p.x += reaction.bodyX + ambient.x; p.dy += reaction.bodyY + ambient.y;
  p.blink = p.blink || reaction.closed;
  const int x = p.x, y = p.dy;
  const uint16_t yellow = rgb(247, 201, 59), sun = rgb(255, 223, 89);
  const uint16_t orange = rgb(242, 143, 54), shade = rgb(199, 103, 41);
  const uint16_t green = rgb(81, 153, 76), blue = rgb(53, 111, 160);
  conurePerch(c, scene);
  // Long blue-green tail, warm breast, zygodactyl toes and overlapping feathers.
  c.triangle(x - 14, 119 + y, x + 14, 119 + y, x, 161 + y, green);
  c.triangle(x - 7, 125 + y, x + 8, 125 + y, x + 3, 160 + y, blue);
  c.line(x - 3, 127 + y, x, 152 + y, rgb(124, 181, 96), 1);
  c.oval(x, 110 + y, 27, 28, shade);
  c.oval(x - 1, 106 + y, 26, 28, yellow);
  c.oval(x, 113 + y, 18, 20, orange);
  c.oval(x - 4, 105 + y, 12, 16, rgb(251, 174, 58));
  for (int feather = -1; feather <= 1; ++feather) {
    c.line(x + feather * 8 - 3, 119 + y, x + feather * 8, 122 + y, yellow);
    c.line(x + feather * 8, 122 + y, x + feather * 8 + 3, 119 + y, yellow);
  }
  for (int sign = -1; sign <= 1; sign += 2) {
    const int footY = 134 + y + sign * ambient.step / 2;
    c.line(x + sign * 12, 128 + y, x + sign * 12, footY, rgb(90, 85, 72), 2);
    for (int toe = -1; toe <= 1; ++toe) {
      c.line(x + sign * 12, footY, x + sign * 12 + toe * 5, footY + 4, rgb(192, 176, 138), 1);
      c.dot(x + sign * 12 + toe * 5, footY + 5, ink);
    }
  }
  if (reaction.id < 0) conureWings(c, reaction, ambient, p);
  HeadCanvas head{c, x, 78 + y, reaction.headX, reaction.headY, reaction.tilt};
  head.oval(x, 79 + y, 32, 29, shade);
  head.oval(x - 1, 75 + y, 31, 28, yellow);
  head.oval(x - 5, 68 + y, 24, 20, sun);
  head.oval(x, 84 + y, 26, 20, orange);
  head.line(x - 12, 51 + y, x - 10, 47 + y, yellow, 2);
  head.line(x - 3, 49 + y, x - 1, 46 + y, sun, 1);
  head.oval(x - 21, 87 + y, 7, 6, rgb(248, 170, 71));
  head.oval(x + 21, 87 + y, 7, 6, rgb(235, 116, 41));
  for (int sign = -1; sign <= 1; sign += 2) {
    head.oval(x + sign * 14, 76 + y, 8, 9, rgb(230, 219, 181));
    head.oval(x + sign * 14, 76 + y, 6, 7, cream);
  }
  Pose eyes = p; eyes.dancing = eyes.dancing || reaction.happy;
  smallEyes(head, x + ambient.gaze, 76 + y, 14, eyes);
  // Broad hooked upper bill with a narrow lower mandible identifies a parrot.
  head.oval(x, 90 + y, 7, 9, rgb(44, 48, 45));
  head.oval(x - 2, 87 + y, 4, 5, rgb(79, 83, 74));
  head.triangle(x - 6, 92 + y, x + 6, 92 + y, x + 2, 103 + y, rgb(36, 41, 40));
  head.line(x - 2, 88 + y, x + 2, 87 + y, rgb(137, 141, 121));
  if (reaction.openMouth || p.dancing) head.oval(x + 1, 100 + y, 3, 2, rgb(224, 150, 120));
  if (reaction.id >= 0) conureWings(c, reaction, ambient, p);
  if (!p.asleep && action == 1) {
    c.oval(x + 2, 106 - int(ms / 220 % 3), 6, 5, mint);
    c.line(x + 2, 101, x + 5, 99, green);
  }
  otherPetEffects(c, p, ms, action);
}

}  // namespace

bool hitTestPet(int x, int y, const Snapshot& state, uint32_t animationMs,
                const Settings& settings, int actionAnimation,
                int reaction, uint32_t reactionMs) {
  // Reject before subtracting coordinates, including hostile integer extremes.
  if (x < 0 || x >= 240 || y < 46 || y > 170) return false;
  const PetMotion motion(state, settings, animationMs, actionAnimation, reaction, reactionMs);
  const ReactionPose& r = motion.reaction;
  Pose p(state, animationMs, r.id >= 0 ? 0 : actionAnimation);
  p.x += r.bodyX + motion.ambient.x;
  p.dy += r.bodyY + motion.ambient.y;
  const int dx = x - p.x;
  // Broad body padding includes fur, ears, walking paws, tail swishes and
  // fluttering feathers. The conure's narrow tail extends below this body.
  if (dx >= -64 && dx <= 64 && y <= 150 + p.dy) return true;
  if (settings.animal == Animal::SunConure && dx >= -20 && dx <= 20 &&
      y >= 132 + p.dy && y <= 164 + p.dy) return true;
  if (r.id >= 0) {
    for (int side = 0; side < 2; ++side) {
      const int handX = p.x + r.handX[side], handY = p.dy + r.handY[side];
      if (x >= handX - 20 && x <= handX + 20 &&
          y >= handY - 14 && y <= handY + 14) return true;
    }
  }
  return false;
}

int hitTestAction(int x, int y) {
  if (y < kButtonTop - kTouchPaddingY ||
      y >= kButtonTop + kButtonHeight + kTouchPaddingY) return -1;
  for (int action = 0; action < 4; ++action) {
    const int left = kButtonLeft + action * kButtonPitch;
    const int width = action == 3 ? kMenuWidth : kButtonWidth;
    if (x >= left - kTouchPaddingX &&
        x < left + width + kTouchPaddingX) return action;
  }
  return -1;
}

void drawPet(uint16_t* pixels, const Snapshot& state, unsigned selectedAction,
             const char* statusMessage, uint32_t animationMs,
             int actionAnimation, const Hud& hud, const Settings& settings, const char* speech,
             int reaction, uint32_t reactionMs) {
  if (!pixels) return;
  Canvas c = {pixels};
  drawScene(c, settings.scene);
  char petName[13];
  hudLabel(petName, settings.name, "Moss");
  const bool largeName = strlen(petName) <= 6;
  c.centered(largeName ? 13 : 16, petName, cream, largeName ? 2 : 1);
  if (settings.showSubtitle) {
    const char* subtitle = settings.animal == Animal::Cat ? "LITTLE CAT"
        : settings.animal == Animal::Frog ? "LITTLE FROG"
        : settings.animal == Animal::SunConure ? "SUN CONURE" : "LITTLE SLOTH";
    c.centered(29, subtitle, muted);
  }
  drawHud(c, hud);

  const bool asleep = state.sleeping != 0;
  const PetMotion motion(state, settings, animationMs, actionAnimation, reaction, reactionMs);
  const ReactionPose& pose = motion.reaction;
  const AmbientPose& ambient = motion.ambient;
  if (pose.id >= 0) actionAnimation = 0;
  switch (settings.animal) {
    case Animal::Cat: drawCat(c, state, animationMs, actionAnimation, pose, ambient); break;
    case Animal::Frog: drawFrog(c, state, animationMs, actionAnimation, pose, ambient); break;
    case Animal::SunConure: drawConure(c, state, animationMs, actionAnimation, pose, ambient, settings.scene); break;
    default: drawSloth(c, state, animationMs, actionAnimation, pose, ambient); break;
  }

  if (speech && *speech) drawPetSpeech(pixels, speech);

  meter(c, 22, "FOOD", state.fullness, mint);
  meter(c, 91, "JOY", state.happiness, gold);
  meter(c, 160, "REST", state.energy, rgb(157, 185, 221), asleep, animationMs);

  // Bound the display message so callers can safely supply longer strings.
  char message[29] = {0};
  if (statusMessage) {
    size_t i = 0;
    for (; i < sizeof(message) - 1 && statusMessage[i]; ++i)
      message[i] = statusMessage[i];
  }
  c.centered(39, message, cream);

  const char* names[] = {"FEED", "PLAY", asleep ? "WAKE" : "NAP"};
  const UiIcon icons[] = {UiIcon::Food, UiIcon::Play, asleep ? UiIcon::Wake : UiIcon::Sleep};
  for (unsigned action = 0; action < 3; ++action) {
    const int x = kButtonLeft + static_cast<int>(action) * kButtonPitch;
    const bool selected = selectedAction == action;
    c.roundRect(x, kButtonTop, kButtonWidth, kButtonHeight, 7,
                selected ? gold : rgb(42, 70, 62));
    c.roundRect(x + 1, kButtonTop + 1, kButtonWidth - 2, kButtonHeight - 2, 6,
                selected ? rgb(66, 69, 46) : rgb(24, 49, 45));
    const int width = static_cast<int>(strlen(names[action])) * 6 - 1;
    const int left = x + (kButtonWidth - 18 - width) / 2;
    drawUiIcon(c, left, kButtonTop + 3, icons[action], selected ? gold : muted);
    c.text(left + 18, kButtonTop + 8, names[action], selected ? gold : muted);
  }
  const bool menuSelected = selectedAction == 3;
  c.roundRect(200, kButtonTop, kMenuWidth, kButtonHeight, 7,
              menuSelected ? gold : rgb(42, 70, 62));
  c.roundRect(201, kButtonTop + 1, kMenuWidth - 2, kButtonHeight - 2, 6,
              menuSelected ? rgb(66, 69, 46) : rgb(24, 49, 45));
  const uint16_t menuColor = menuSelected ? gold : muted;
  c.rect(207, 204, 14, 2, menuColor);
  c.rect(207, 209, 14, 2, menuColor);
  c.rect(207, 214, 14, 2, menuColor);
}

}  // namespace sloth
