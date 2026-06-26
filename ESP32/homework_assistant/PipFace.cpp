/* ============================================================================
 *  PipFace — implementation. See PipFace.h for the public API.
 * ----------------------------------------------------------------------------
 *  Lifted nearly byte-for-byte from the original Pip_ESP32.ino reference
 *  sketch — the drawing helpers and per-state logic are untouched so the
 *  visual output is identical to the design. Structural changes:
 *    - setup()/loop() removed; their bodies wrapped as Pip::begin/tick().
 *    - tick() is non-blocking — pacing via millis() instead of delay(33).
 *    - state-cycling demo loop dropped (lives in examples/PipDemo/ now).
 *    - bottom status strip cached + redrawn only on change.
 *    - new Pip::setDeviceStatus() maps the project's deviceState vocabulary
 *      onto the 12-emotion enum (table in PipFace.h).
 *    - bottom strip now renders Hebrew (and any UTF-8) via u8g2's
 *      unifont_t_hebrew, with a tiny bidi-lite pass to handle RTL display.
 *    - rendering moved to a FreeRTOS task so the face stays responsive
 *      while the main loop is blocked on HTTPS / STT / audio playback /
 *      I2S recording. tick() is now a no-op kept for API compatibility.
 *    - per-state drawing refreshed to match the new vector design:
 *        HAPPY      pink cheek blush dots
 *        LISTENING  two phased expanding sound rings (+ pulsing eyes)
 *        ENCOURAGING  single slow warm ring
 *        SPEAKING   ellipse mouth with rx/ry lip-sync (was rect)
 *        SLEEPY     yawning ellipse (was round-rect)
 *        OOPS       wide oval mouth + spark lines (was sweat drop)
 *        CONCERNED  straight diagonal worry brows (was curves)
 *        PROUD      4-point twinkle stars (was 5-pt stars)
 *        CELEBRATING confetti + twinkle flourish stars
 * ========================================================================== */

#include "PipFace.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>
#include <math.h>
#include <string.h>

namespace {  // ---- private to this translation unit ----

TFT_eSPI  tft  = TFT_eSPI();
TFT_eSprite face = TFT_eSprite(&tft);     // 240x240 face buffer
U8g2_for_TFT_eSPI u8f;                    // Unicode/Hebrew text engine

enum PipState {
  IDLE, SPEAKING, LISTENING, THINKING,
  HAPPY, PROUD, CELEBRATING, ENCOURAGING,
  CONCERNED, PLAYFUL, SLEEPY, OOPS,
  STATE_COUNT
};
PipState state = IDLE;

// Cached strip — redrawn only when text or stars change. Protected by
// stripMux because the writer (Pip::setStrip, called from the main task)
// and the reader (the face task) live on different threads.
char           stripText[64] = "";
int            stripStars   = -1;   // sentinel: forces first paint
bool           stripDirty   = true;
portMUX_TYPE   stripMux     = portMUX_INITIALIZER_UNLOCKED;

// Handle for the background render task spawned in begin().
TaskHandle_t   faceTaskHandle = nullptr;

// Colours (RGB565), filled in begin().
uint16_t BG, GLOW, GLOWD, GOLD, PINK, WHT, SCREEN_BG;

// Face layout within the 240x240 sprite.
const int EX_L = 84, EX_R = 156, EYE_Y = 118, MX = 120, MY = 168;

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return tft.color565(r, g, b); }

// ============================ small draw helpers ============================

// thick quadratic curve: sample a Bezier and join points with wide lines
void wideCurve(int x0,int y0, int cx,int cy, int x1,int y1, uint16_t col, int w) {
  const int N = 16;
  int px = x0, py = y0;
  for (int i = 1; i <= N; i++) {
    float t = (float)i / N, u = 1 - t;
    int x = (int)(u*u*x0 + 2*u*t*cx + t*t*x1);
    int y = (int)(u*u*y0 + 2*u*t*cy + t*t*y1);
    face.drawWideLine(px, py, x, y, w, col);
    px = x; py = y;
  }
}
// glowing variants: dim halo first, then the bright shape on top
void glowRoundRect(int x,int y,int w,int h,int r) {
  face.fillRoundRect(x-2, y-2, w+4, h+4, r+2, GLOWD);
  face.fillRoundRect(x,   y,   w,   h,   r,   GLOW);
}
void glowCircle(int x,int y,int r) {
  face.fillCircle(x, y, r+2, GLOWD);
  face.fillCircle(x, y, r,   GLOW);
}
void glowCurve(int x0,int y0,int cx,int cy,int x1,int y1,int w) {
  wideCurve(x0,y0,cx,cy,x1,y1, GLOWD, w+3);
  wideCurve(x0,y0,cx,cy,x1,y1, GLOW,  w);
}
// 5-point star (triangle fan)
void fillStar(int cx,int cy,int r,uint16_t col) {
  float pts[10][2];
  for (int i = 0; i < 10; i++) {
    float a = M_PI/5*i - M_PI/2;
    float rad = (i % 2 == 0) ? r : r*0.45f;
    pts[i][0] = cx + cosf(a)*rad;
    pts[i][1] = cy + sinf(a)*rad;
  }
  for (int i = 0; i < 10; i++) {
    int j = (i+1) % 10;
    face.fillTriangle(cx, cy, pts[i][0], pts[i][1], pts[j][0], pts[j][1], col);
  }
}

// caret "^" happy eye
void happyEye(int ex,int y) { glowCurve(ex-16,y+7, ex,y-9, ex+16,y+7, 9); }
// open capsule eye
void capsuleEye(int ex,int y) { glowRoundRect(ex-15, y-21, 30, 42, 15); }

// pink "blush" dot — used on HAPPY's cheeks (matches the new vector design).
void drawBlush(int cx, int cy, int r) {
  face.fillCircle(cx, cy, r+1, GLOWD);   // soft halo
  face.fillCircle(cx, cy, r,   PINK);
}

// Glowing filled ellipse — same halo trick as glowRoundRect / glowCircle.
// Used by SPEAKING (morphing mouth), SLEEPY (yawn), OOPS (wide mouth).
void glowEllipse(int cx, int cy, int rx, int ry) {
  if (rx < 1) rx = 1;
  if (ry < 1) ry = 1;
  face.fillEllipse(cx, cy, rx+2, ry+2, GLOWD);
  face.fillEllipse(cx, cy, rx,   ry,   GLOW);
}

// Expanding sound ring used by LISTENING (two phased rings) and
// ENCOURAGING (single slow ring). phase ∈ [0,1) — 0 = freshly born at
// [startR], 1 = faded out at [endR]. Dims the stroke past phase 0.5
// so the ring appears to fade as it grows.
void drawSoundRing(int cx, int cy, int startR, int endR, float phase) {
  int r = startR + (int)((endR - startR) * phase);
  uint16_t col = (phase < 0.55f) ? GLOW : GLOWD;
  // Thick ring approximated by 3 adjacent unfilled circles.
  face.drawCircle(cx, cy, r,     col);
  face.drawCircle(cx, cy, r + 1, col);
  face.drawCircle(cx, cy, r - 1, col);
}

// 4-point "twinkle" sparkle — sharper than the 5-pt fillStar.
// Used by PROUD's overlay and CELEBRATING flourish stars.
void drawTwinkle(int cx, int cy, int r, uint16_t col) {
  int v = (int)(r * 0.42f); if (v < 1) v = 1;
  // vertical lens
  face.fillTriangle(cx, cy - r, cx - v, cy, cx, cy + r, col);
  face.fillTriangle(cx, cy - r, cx + v, cy, cx, cy + r, col);
  // horizontal lens
  face.fillTriangle(cx - r, cy, cx, cy - v, cx + r, cy, col);
  face.fillTriangle(cx - r, cy, cx, cy + v, cx + r, cy, col);
}

// ================================ the face ================================
void drawAntenna(int dy, uint32_t t) {
  uint16_t bulb = (state==PROUD||state==CELEBRATING) ? GOLD : GLOW;
  int br = 6 + (int)(2*sin(t/300.0));                  // gentle pulse
  face.drawWideLine(120, 44+dy, 120, 24+dy, 6, GLOW);
  face.fillCircle(120, 16+dy, br+2, GLOWD);
  face.fillCircle(120, 16+dy, br, bulb);
}

void drawEyes(int dy, uint32_t t) {
  int y = EYE_Y + dy;
  bool blink = (t % 3600) > 3450;                      // quick, occasional
  switch (state) {
    case HAPPY: case PROUD:
      happyEye(EX_L,y); happyEye(EX_R,y); break;
    case CELEBRATING:
      fillStar(EX_L,y,18,GLOW); fillStar(EX_R,y,18,GLOW); break;
    case ENCOURAGING:                                   // wink (right)
      if (blink) glowRoundRect(EX_L-15,y-3,30,7,3); else capsuleEye(EX_L,y);
      happyEye(EX_R,y); break;
    case PLAYFUL:                                        // wink (left)
      happyEye(EX_L,y);
      capsuleEye(EX_R,y); break;
    case LISTENING: {                                    // pulsing round eyes
      int r = 16 + (int)(2*sin(t/220.0));
      glowCircle(EX_L,y,r); glowCircle(EX_R,y,r); break; }
    case THINKING: {                                     // small, glancing up
      int gx = (int)(4*sin(t/700.0));
      glowCircle(EX_L+gx,y-6,12); glowCircle(EX_R+gx,y-6,12); break; }
    case CONCERNED:
      glowCircle(EX_L,y,11); glowCircle(EX_R,y,11);
      // Straight diagonal worry brows — outer-top down to inner-low
      // (was a curve; new vector design draws this as a line).
      face.drawWideLine(66,  98+dy, 98,  88+dy, 10, GLOWD);
      face.drawWideLine(66,  98+dy, 98,  88+dy, 7,  GLOW);
      face.drawWideLine(174, 98+dy, 142, 88+dy, 10, GLOWD);
      face.drawWideLine(174, 98+dy, 142, 88+dy, 7,  GLOW);
      break;
    case OOPS:
      glowCircle(EX_L,y,18); glowCircle(EX_R,y,18);
      glowCurve(EX_L-16,y-26, EX_L,y-34, EX_L+16,y-28, 6);   // raised brows
      glowCurve(EX_R+16,y-26, EX_R,y-34, EX_R-16,y-28, 6); break;
    case SLEEPY:
      glowCurve(EX_L-16,y, EX_L,y+12, EX_L+16,y, 9);
      glowCurve(EX_R-16,y, EX_R,y+12, EX_R+16,y, 9); break;
    case IDLE: case SPEAKING: default:
      if (blink) { glowRoundRect(EX_L-15,y-3,30,7,3); glowRoundRect(EX_R-15,y-3,30,7,3); }
      else       { capsuleEye(EX_L,y); capsuleEye(EX_R,y); }
      break;
  }
}

void drawMouth(int dy, uint32_t t) {
  int y = MY + dy;
  switch (state) {
    case SPEAKING: {
      // Ellipse mouth — rx + ry pulse independently (lip-sync feel).
      // Was a rect — the new vector design uses an animated ellipse.
      int rx = 17 + (int)(6 * fabs(cos(t / 180.0)));
      int ry =  5 + (int)(9 * fabs(sin(t / 120.0)));
      glowEllipse(MX, y, rx, ry);
      break;
    }
    case LISTENING:
      glowCircle(MX, y, 10); break;
    case THINKING:
      glowCurve(MX-15,y, MX,y-4, MX+15,y+2, 7); break;
    case HAPPY: case CELEBRATING:
      glowCurve(MX-26,y-8, MX,y+30, MX+26,y-8, 12); break;  // big smile
    case PROUD: case ENCOURAGING:
      glowCurve(MX-22,y-6, MX,y+20, MX+22,y-6, 10); break;
    case CONCERNED:
      glowCurve(MX-26,y+6, MX,y-8, MX+26,y+6, 9); break;    // gentle frown
    case PLAYFUL:
      glowCurve(MX-22,y-6, MX,y+22, MX+22,y-6, 10);
      face.fillRoundRect(MX-9, y+8, 18, 14, 6, PINK); break; // tongue
    case SLEEPY: {
      // Yawning ellipse — height oscillates wide.
      int ry = 7 + (int)(13 * fabs(sin(t / 1500.0)));
      glowEllipse(MX, y + 4, 14, ry);
      break;
    }
    case OOPS:
      // Wide oval mouth (was a small circle) — looks startled.
      glowEllipse(MX, y + 4, 13, 15);
      break;
    case IDLE: default:
      glowCurve(MX-20,y-2, MX,y+18, MX+20,y-2, 9); break;   // calm smile
  }
}

void drawOverlay(int dy, uint32_t t) {
  switch (state) {
    case HAPPY:
      // Pink cheek blush dots — matches the new vector design.
      drawBlush(52,  150 + dy, 12);
      drawBlush(188, 150 + dy, 12);
      break;
    case LISTENING: {
      // Two expanding sound rings, phased so they overlap nicely.
      float p1 = (float)((t       ) % 1800) / 1800.0f;
      float p2 = (float)((t + 900 ) % 1800) / 1800.0f;
      drawSoundRing(120, 120 + dy, 92, 118, p1);
      drawSoundRing(120, 120 + dy, 92, 118, p2);
      break;
    }
    case ENCOURAGING: {
      // Single slower warm ring.
      float p = (float)(t % 2400) / 2400.0f;
      drawSoundRing(120, 120 + dy, 96, 124, p);
      break;
    }
    case THINKING: {
      int n = (t / 350) % 4;
      for (int i = 0; i < 3; i++) if (i < n) glowCircle(196+i*15, 70-i*6+dy, 5+i);
      break;
    }
    case SLEEPY: {
      face.setTextColor(GLOW);
      face.drawString("z", 196, 70+dy, 2);
      face.drawString("Z", 214, 50+dy, 4);
      break;
    }
    case PROUD: {
      // 4-point twinkle stars (was 5-pt fillStar). Sharper, more "spark".
      drawTwinkle(56,  90  + dy, 9, GOLD);
      drawTwinkle(196, 104 + dy, 8, GOLD);
      drawTwinkle(186, 58  + dy, 7, GOLD);
      break;
    }
    case CELEBRATING: {                                  // confetti + twinkles
      int yy = (t/6) % 220;
      uint16_t cs[3] = { GOLD, PINK, GLOW };
      for (int i = 0; i < 6; i++) {
        int cx = 30 + i*34, cy = (yy + i*36) % 220;
        face.fillRect(cx, cy, 9, 9, cs[i % 3]);
      }
      drawTwinkle(44,  64 + dy, 10, GOLD);
      drawTwinkle(200, 78 + dy, 9,  GOLD);
      break;
    }
    case OOPS: {
      // Small "spark" lines around the face — was a sweat-drop circle.
      face.drawWideLine(40,  110 + dy, 28,  104 + dy, 4, GOLD);
      face.drawWideLine(200, 110 + dy, 212, 104 + dy, 4, GOLD);
      face.drawWideLine(44,  140 + dy, 32,  144 + dy, 4, GOLD);
      break;
    }
    default: break;
  }
}

void drawFace(uint32_t t) {
  face.fillSprite(BG);
  int bob = (int)round(3.0*sin(t/520.0));
  if (state == CELEBRATING) bob = (int)round(7.0*sin(t/170.0));
  else if (state == HAPPY)  bob = (int)round(5.0*sin(t/220.0));
  else if (state == SLEEPY) bob = (int)round(2.0*sin(t/950.0));
  drawAntenna(bob, t);
  drawEyes(bob, t);
  drawMouth(bob, t);
  drawOverlay(bob, t);
  face.pushSprite(0, 0);
}

// 5-point filled star, drawn directly to the TFT (not the sprite).
// Mirrors the in-sprite fillStar(); kept separate so we don't need to
// allocate a tiny sprite for the strip.
void fillStarTFT(int cx, int cy, int r, uint16_t col) {
  float pts[10][2];
  for (int i = 0; i < 10; i++) {
    float a = M_PI/5*i - M_PI/2;
    float rad = (i % 2 == 0) ? r : r*0.45f;
    pts[i][0] = cx + cosf(a)*rad;
    pts[i][1] = cy + sinf(a)*rad;
  }
  for (int i = 0; i < 10; i++) {
    int j = (i+1) % 10;
    tft.fillTriangle(cx, cy, pts[i][0], pts[i][1], pts[j][0], pts[j][1], col);
  }
}

// ---- Bidi-lite for RTL display on a LTR rendering engine ----
//
// Takes a UTF-8 string in logical order and returns its visual order so
// that drawing it left-to-right makes a Hebrew reader see it right-to-left
// correctly. Numbers and Latin letters stay in their natural order within
// their own runs; Hebrew runs are reversed; the run order itself is
// reversed.
//
// Algorithm (works for Hebrew + digits + Latin punctuation, the only
// scripts this project produces):
//   1. Reverse the entire codepoint sequence.
//   2. Within that reversed sequence, reverse each run of "LTR-strong"
//      codepoints (ASCII digits + Latin letters) back to its natural order.
// Hebrew letters, spaces, and punctuation stay reversed — which is exactly
// what RTL display needs. This handles the project's prompts ("כמה זה 5
// כפול 8?", "איך אומרים book באנגלית?", etc.) without pulling in a full
// Unicode Bidirectional Algorithm.
bool isLtrStrong(uint32_t cp) {
  return (cp >= '0' && cp <= '9') ||
         (cp >= 'a' && cp <= 'z') ||
         (cp >= 'A' && cp <= 'Z');
}

String bidiToVisual(const char* utf8) {
  constexpr int MAX_CPS = 128;
  uint32_t cps[MAX_CPS];
  int n = 0;

  // 1. UTF-8 -> codepoints.
  const uint8_t* p = (const uint8_t*)utf8;
  while (*p && n < MAX_CPS) {
    uint32_t cp = 0;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0) {
      cp  = (uint32_t)(*p++ & 0x1F) << 6;
      cp |= (uint32_t)(*p++ & 0x3F);
    } else if ((*p & 0xF0) == 0xE0) {
      cp  = (uint32_t)(*p++ & 0x0F) << 12;
      cp |= (uint32_t)(*p++ & 0x3F) << 6;
      cp |= (uint32_t)(*p++ & 0x3F);
    } else {
      p++;  // 4-byte or invalid — skip
      continue;
    }
    cps[n++] = cp;
  }

  // 2. Reverse the whole sequence.
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    uint32_t t = cps[i]; cps[i] = cps[j]; cps[j] = t;
  }

  // 3. Re-reverse LTR-strong runs (numbers + Latin) so they read naturally.
  int i = 0;
  while (i < n) {
    if (isLtrStrong(cps[i])) {
      int j = i;
      while (j < n && isLtrStrong(cps[j])) j++;
      for (int a = i, b = j - 1; a < b; a++, b--) {
        uint32_t t = cps[a]; cps[a] = cps[b]; cps[b] = t;
      }
      i = j;
    } else {
      i++;
    }
  }

  // 4. Codepoints -> UTF-8.
  String out;
  out.reserve(n * 3);
  for (int k = 0; k < n; k++) {
    uint32_t cp = cps[k];
    if (cp < 0x80) {
      out += (char)cp;
    } else if (cp < 0x800) {
      out += (char)(0xC0 | (cp >> 6));
      out += (char)(0x80 | (cp & 0x3F));
    } else {
      out += (char)(0xE0 | (cp >> 12));
      out += (char)(0x80 | ((cp >> 6) & 0x3F));
      out += (char)(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

// Bottom status strip — Hebrew-capable text on the right (RTL aligned),
// star icon + count on the left. Text rendered via u8g2's Hebrew unifont
// so כמה זה 5 כפול 8? displays correctly; the star count uses the
// built-in ASCII font 4 since it's just digits.
// Takes snapshots so the render isn't racing with Pip::setStrip().
void drawStrip(const char* textSnap, int starsSnap) {
  tft.fillRect(0, 240, 240, 80, SCREEN_BG);
  tft.drawFastHLine(0, 242, 240, GLOWD);

  // ---- stars + count, left side (less prominent badge in Hebrew layout) ----
  int leftCursor = 16;
  if (starsSnap >= 0) {
    fillStarTFT(22, 282, 10, GOLD);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", starsSnap);
    tft.setTextColor(GOLD, SCREEN_BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(buf, 38, 282, 4);
    leftCursor = 38 + tft.textWidth(buf, 4) + 8;
  }

  // ---- question text, right side, bidi-reordered ----
  if (textSnap[0] != '\0') {
    String visual = bidiToVisual(textSnap);
    u8f.setFont(u8g2_font_unifont_t_hebrew);
    u8f.setForegroundColor(WHT);
    u8f.setBackgroundColor(SCREEN_BG);
    const int avail = 240 - 16 - leftCursor;   // px available for the text
    int textW = u8f.getUTF8Width(visual.c_str());
    if (textW > avail) {
      // Too wide (English questions are far wider than Hebrew/digits in this
      // font): trim whole codepoints off the end until it fits, add an
      // ellipsis, and LEFT-align so it never runs off the right edge. The
      // full question is still spoken aloud.
      while (visual.length() > 0 &&
             u8f.getUTF8Width((visual + "...").c_str()) > avail) {
        int i = visual.length() - 1;           // drop one whole UTF-8 codepoint
        while (i > 0 && ((uint8_t)visual[i] & 0xC0) == 0x80) i--;
        visual.remove(i);
      }
      visual += "...";
      u8f.setCursor(leftCursor, 290);
    } else {
      u8f.setCursor(240 - 16 - textW, 290);    // right-align, 16 px right pad
    }
    u8f.print(visual);                          // baseline ~16 px above bottom
  }
}

// Background render task — drives the face at ~30 fps independently of the
// main loop. This is what makes Pip responsive even when the partner's
// main code is blocked on HTTPS, STT, audio playback, or recording.
void faceTask(void*) {
  for (;;) {
    drawFace(millis());

    // Strip: snapshot inside critical section, render outside.
    bool render = false;
    char textSnap[64];
    int  starsSnap = -1;
    portENTER_CRITICAL(&stripMux);
    if (stripDirty) {
      memcpy(textSnap, stripText, sizeof(textSnap));
      starsSnap = stripStars;
      stripDirty = false;
      render = true;
    }
    portEXIT_CRITICAL(&stripMux);
    if (render) drawStrip(textSnap, starsSnap);

    vTaskDelay(pdMS_TO_TICKS(33));    // ~30 fps; yields the CPU
  }
}

// Maps an emotion label to the internal enum. Returns true if matched.
bool resolveEmotion(const char* label, PipState& out) {
  if (label == nullptr) return false;
  struct { const char* k; PipState s; } M[] = {
    {"idle",IDLE},{"speaking",SPEAKING},{"listening",LISTENING},{"thinking",THINKING},
    {"happy",HAPPY},{"proud",PROUD},{"celebrating",CELEBRATING},{"encouraging",ENCOURAGING},
    {"concerned",CONCERNED},{"playful",PLAYFUL},{"sleepy",SLEEPY},{"oops",OOPS},
  };
  for (auto &m : M) if (strcmp(m.k, label) == 0) { out = m.s; return true; }
  return false;
}

}  // unnamed namespace

// ================================ Pip:: API ================================
namespace Pip {

bool begin() {
  tft.init();
  tft.setRotation(0);                 // 0 = portrait 240x320 (face on top)
  SCREEN_BG = TFT_BLACK;
  tft.fillScreen(SCREEN_BG);

  BG    = TFT_BLACK;
  GLOW  = rgb(0x5E, 0xE7, 0xE7);      // Pip cyan
  GLOWD = rgb(0x1C, 0x5E, 0x5E);      // dim cyan halo (glow step)
  GOLD  = rgb(0xFF, 0xC9, 0x3C);
  PINK  = rgb(0xFF, 0x8F, 0xB1);
  WHT   = TFT_WHITE;

  face.setColorDepth(16);
  if (!face.createSprite(240, 240)) { // ~115 KB — needs PSRAM
    tft.drawString("Sprite alloc failed - enable PSRAM", 6, 6, 2);
    return false;
  }

  // u8g2 wrapper — used by the bottom strip for Hebrew/Unicode rendering.
  // Mode 1 = transparent background per glyph (we paint the strip's solid
  // background separately in drawStrip()).
  u8f.begin(tft);
  u8f.setFontMode(1);
  u8f.setFontDirection(0);

  // Paint one frame synchronously so the screen isn't blank between
  // begin() returning and the render task's first tick.
  drawFace(millis());

  // Spawn the background render task on Core 1 (where Arduino's main loop
  // lives — WiFi/network are on Core 0, so the face stays smooth even
  // during network operations because FreeRTOS preempts blocked tasks).
  // Low priority (1) so it never starves real work.
  if (faceTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(
      faceTask, "PipFace", 4096, nullptr, 1, &faceTaskHandle, 1);
  }
  return true;
}

void tick() {
  // No-op: the face renders on its own FreeRTOS task spawned in begin().
  // Kept in the API so existing main loops that call Pip::tick() still
  // compile and don't need editing.
}

void setEmotion(const char* label) {
  PipState s;
  if (resolveEmotion(label, s)) state = s;
}

void setDeviceStatus(const char* status, int mood) {
  if (status == nullptr) return;
  if      (strcmp(status, "idle")      == 0) state = IDLE;
  else if (strcmp(status, "asking")    == 0) state = SPEAKING;
  else if (strcmp(status, "listening") == 0) state = LISTENING;
  else if (strcmp(status, "break")     == 0) state = SLEEPY;
  else if (strcmp(status, "error")     == 0) state = OOPS;
  else if (strcmp(status, "feedback")  == 0) {
    if      (mood >= 4) state = PROUD;
    else if (mood >= 1 && mood <= 2) state = CONCERNED;
    else                state = ENCOURAGING;   // mood == 3 or unknown (-1)
  }
  // else: unknown status — ignore, keep current state.
}

void setStrip(const char* questionOrLabel, int stars) {
  const char* incoming = questionOrLabel ? questionOrLabel : "";
  portENTER_CRITICAL(&stripMux);
  if (strcmp(stripText, incoming) != 0 || stripStars != stars) {
    strncpy(stripText, incoming, sizeof(stripText) - 1);
    stripText[sizeof(stripText) - 1] = '\0';
    stripStars = stars;
    stripDirty = true;
  }
  portEXIT_CRITICAL(&stripMux);
}

}  // namespace Pip
