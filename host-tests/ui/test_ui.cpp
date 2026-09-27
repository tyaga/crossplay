// Host tests for this fork's screens. No device, no PlatformIO, no renderer:
// FreeInkUI is freestanding C++17 and the screen builders were written to stay
// that way, so a laptop can build a screen against a fake draw target and ask
// what it drew and what it made tappable.
//
// This is the half of the app that used to be untestable. The chess *rules* had
// 2940 assertions and the screens had none, which was backwards: the rules are
// stable and the screens change every time Mario asks for something. Two real
// bugs this file would have caught the day they were written are pinned below.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/apps_local/ShelfScreen.h"
#include "../../src/apps_local/battleship/BattleshipScreens.h"
#include "../../src/apps_local/hackernews/HackerNewsScreens.h"
#include "../../src/apps_local/link/LinkScreens.h"
#include "../../src/apps_local/player/PlayerAvatar.h"
#include "../../src/apps_local/player/PlayerScreen.h"
#include "../../src/apps_local/readingstats/StatsScreens.h"
#include "../../src/apps_local/solitaire/SolitaireScreens.h"
#include "../../src/apps_local/study/StudyScreens.h"
#include "../../src/apps_local/ui/ToyboxFormat.h"
#include "../../src/apps_local/ui/ToyboxIcons.h"
#include "../../src/apps_local/ui/ToyboxText.h"
#include "../../src/apps_local/ui/ToyboxWrappedText.h"
#include "../../src/apps_local/wallpapers/WallpapersCore.h"
#include "../../src/apps_local/wallpapers/WallpapersScreens.h"
#include "../../src/apps_local/xkcd/XkcdScreens.h"

namespace fui = freeink::ui;

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL %s:%d  %s\n", "test_ui.cpp", line, what);
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// Records what was drawn instead of drawing it. Text is what the assertions
// mostly care about; the rects matter for the hit-testing checks.
class FakeTarget final : public fui::DrawTarget {
 public:
  struct TextRun {
    fui::Rect rect;
    std::string text;
    fui::Color color;
    // The whole style, not just the ink. A single-line run wider than its rect
    // is a silent truncation -- the SDK ellipsizes and logs nothing -- and that
    // is what put "IN THE BAR: TILE = TROOPS IN HAND, TRIANGLE = LEFT ..." on
    // the terrain card for as long as the card existed. It is only assertable
    // if the assertion can see maxLines.
    fui::TextStyle style;
  };

  // An avatar is four stacked 1-bpp masks and no text at all, so without
  // recording these there is nothing to assert about a face: it would draw, or
  // not draw, or draw in the same colour as the bar behind it, and every one of
  // those would look identical from here.
  struct Blit {
    fui::Rect rect;
    const uint8_t* data;
    fui::Color color;
  };

  std::vector<TextRun> texts;
  std::vector<fui::Rect> fills;
  // The paint too, not just the rect. A state expressed only as a different
  // ground -- Battlefield freezing a card -- is otherwise untestable, and it is
  // exactly the kind of state a screenshot will not happen to contain.
  std::vector<fui::Paint> fillPaints;
  std::vector<Blit> blits;
  // Outlines and marks, which used to be dropped on the floor. "Does this look
  // like a button" is a question about a BORDER, so a target that records only
  // text and fills cannot be asked it -- and that is why the forehead start
  // control shipped as a bare headline that three rounds of tests called fine.
  struct Stroke {
    fui::Rect rect;
    uint8_t width;
  };
  std::vector<Stroke> strokes;
  struct Triangle {
    fui::Point a, b, c;
    fui::Color color;
  };
  std::vector<Triangle> triangles;

  // Strokes drawn as LINES, which this target used to throw away. A mark made
  // of lines was therefore invisible to every test: picross draws the player's
  // X and the game's mistake asterisk this way, so "is a mistake still a solid
  // black cell?" and "do the two marks differ?" were questions no assertion
  // could reach, only a screenshot. Recording them costs a vector and turns
  // both into checks.
  struct Segment {
    fui::Point a, b;
    uint8_t width;
    fui::Color color;
  };
  std::vector<Segment> lines;

  // A fixed cell, but not a fixed LINE. A layout that reserves a constant
  // number of pixels for wrapped text is correct at one metric and wrong at
  // every other, and 20 happens to be small enough to hide it: the rules
  // caption was given a flat 132px, which fits four fake lines and not four
  // real ones (the display cut is 45). Tests that care re-run at a taller
  // line, where a hardcoded box overflows exactly as it does on the device.
  int16_t lineH = 20;

  // Every measureText this target is asked for, counted. This is the
  // instrument the reader's cost is stated in: textAreaWalk() asks for one
  // measurement per candidate character position, so the count is the work a
  // wrap does, and it is the same number before and after a change. A device
  // stopwatch is not available to a host suite; an operation count is.
  mutable long measureCalls = 0;

  // A character's width. Ten unless a test is asking what happens when the
  // reading size changes under a wrap that has already been taken.
  int16_t charW = 10;
  // Per-font and per-weight widths. BOTH DEFAULT TO OFF, so the eighty
  // thousand checks that assume a uniform ten-pixel cell are untouched.
  //
  // They exist because a staleness test that only moves `charW` cannot tell
  // whether the code under test passes `style` to measureText at all: every
  // font and every weight would answer the same, so a fingerprint that
  // ignored the style entirely would still change. These can tell.
  std::vector<std::pair<fui::FontId, int16_t>> fontWidths;
  int16_t boldBonus = 0;
  // Extra width whenever `kernSeq` appears in the run. A target whose answer
  // depends on which characters are ADJACENT, which is what kerning is: it
  // cannot be predicted from the width of each character on its own, and it is
  // therefore the one metrics change a per-character fingerprint can miss.
  std::string kernSeq;
  int16_t kernBonus = 0;
  // More than one pair, and a pair may get NARROWER. A real change of cut
  // moves pair widths in both directions, and it is the compensating case --
  // one paragraph gaining a line while another loses one -- that a check
  // counting lines cannot see at all.
  std::vector<std::pair<std::string, int16_t>> kerns;

  fui::Size measureText(const fui::FontId font, const char* text, const fui::TextStyle style) const override {
    ++measureCalls;
    if (text == nullptr) return fui::Size{0, lineH};
    int16_t cell = charW;
    for (const auto& entry : fontWidths) {
      if (entry.first == font) {
        cell = entry.second;
        break;
      }
    }
    if (style.bold) cell = static_cast<int16_t>(cell + boldBonus);
    int32_t w = static_cast<int32_t>(std::strlen(text)) * cell;
    if ((kernBonus != 0 && !kernSeq.empty()) || !kerns.empty()) {
      const std::string run(text);
      if (kernBonus != 0 && !kernSeq.empty()) {
        for (size_t at = run.find(kernSeq); at != std::string::npos; at = run.find(kernSeq, at + 1)) w += kernBonus;
      }
      for (const auto& pair : kerns) {
        if (pair.first.empty()) continue;
        for (size_t at = run.find(pair.first); at != std::string::npos; at = run.find(pair.first, at + 1)) {
          w += pair.second;
        }
      }
    }
    if (w < 0) w = 0;
    return fui::Size{static_cast<int16_t>(w), lineH};
  }
  int16_t lineHeight(const fui::FontId) const override { return lineH; }

  void fill(const fui::Rect rect, const fui::Paint paint, const uint8_t = 0, const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None) {
      fills.push_back(rect);
      fillPaints.push_back(paint);
    }
  }
  void stroke(const fui::Rect rect, const fui::Paint paint, const uint8_t width, const uint8_t = 0,
              const uint8_t = 0xFF) override {
    if (paint.kind != fui::PaintKind::None) strokes.push_back(Stroke{rect, width});
  }
  void line(const fui::Point a, const fui::Point b, const uint8_t width, const fui::Paint paint) override {
    if (paint.kind != fui::PaintKind::None) lines.push_back(Segment{a, b, width, paint.color});
  }
  void triangle(const fui::Point a, const fui::Point b, const fui::Point c, const fui::Paint paint) override {
    triangles.push_back(Triangle{a, b, c, paint.color});
  }
  void text(const fui::Rect rect, const char* text, const fui::TextStyle style) override {
    if (text != nullptr) texts.push_back(TextRun{rect, text, style.color, style});
  }
  void bitmap(const fui::Rect rect, const fui::BitmapRef bitmap, const fui::BitmapMode, const fui::Paint paint = {},
              const fui::Rotation = fui::Rotation::None) override {
    blits.push_back(Blit{rect, bitmap.data, paint.color});
  }

  // Where this exact face was painted, and in what colour. Returns a zero rect
  // unless every one of its layers landed on the *same* rect in the *same*
  // colour, which is the property that matters: the layers are separate
  // bitmaps of one drawing, so a face out of register is a mouth on a forehead.
  //
  // Asked this way rather than "was something drawn near here" because the
  // pointers come from player::avatarFor, so a pass means this name's face and
  // no other.
  fui::Rect faceRect(const player::Avatar& avatar, const fui::Color color) const {
    fui::Rect agreed{};
    bool first = true;
    for (int i = 0; i < player::Avatar::kLayerCount; ++i) {
      if (avatar.layer[i] == nullptr) continue;
      bool found = false;
      for (const auto& blit : blits) {
        if (blit.data != avatar.layer[i]->bits || blit.color != color) continue;
        if (first) {
          agreed = blit.rect;
          first = false;
          found = true;
          break;
        }
        if (blit.rect.x == agreed.x && blit.rect.y == agreed.y && blit.rect.width == agreed.width &&
            blit.rect.height == agreed.height) {
          found = true;
          break;
        }
      }
      if (!found) return fui::Rect{};
    }
    return agreed;
  }

  int layersOf(const player::Avatar& avatar) const {
    int count = 0;
    for (int i = 0; i < player::Avatar::kLayerCount; ++i) {
      if (avatar.layer[i] != nullptr) count++;
    }
    return count;
  }

  bool drew(const char* needle) const {
    for (const auto& run : texts) {
      // cppcheck-suppress useStlAlgorithm
      if (run.text == needle) return true;
    }
    return false;
  }

  bool outlined(const fui::Rect rect) const {
    for (const auto& s : strokes) {
      if (s.width == 0) continue;  // a zero-width stroke draws nothing
      if (s.rect.x == rect.x && s.rect.y == rect.y && s.rect.width == rect.width && s.rect.height == rect.height) {
        return true;
      }
    }
    return false;
  }

  bool triangleInside(const fui::Rect rect, const fui::Color color = fui::Color::Black) const {
    for (const auto& tri : triangles) {
      if (tri.color != color) continue;
      const fui::Point pts[3] = {tri.a, tri.b, tri.c};
      bool all = true;
      for (const auto& p : pts) {
        if (p.x < rect.x || p.x > rect.x + rect.width || p.y < rect.y || p.y > rect.y + rect.height) all = false;
      }
      if (all) return true;
    }
    return false;
  }

  const TextRun* find(const char* needle) const {
    for (const auto& run : texts) {
      if (run.text == needle) return &run;
    }
    return nullptr;
  }
};

// Where the ink actually lands, which is not where the rect is.
//
// GfxRendererTarget places a single-line run at
// `rect.y + max(0, (rect.height - lineHeight) / 2)` and draws from there with
// the y as the top of the ascender box. That rule is restated here rather than
// assumed, so a change to the target's arithmetic fails these checks instead of
// being silently agreed with.
int inkTopIn(const fui::Rect& given, const toybox::CutMetrics& cut) {
  const int offset = given.height - cut.lineHeight > 0 ? (given.height - cut.lineHeight) / 2 : 0;
  return given.y + offset + cut.ascender - cut.inkHeight;
}

// Sea Salt, and every other game on the default faces, binds the three slots to
// the Jersey cuts. Which cut a recorded run was set in is therefore knowable
// from its slot, and that is what turns a rect back into the ink inside it.
const toybox::CutMetrics& cutForSlot(const fui::FontId slot) {
  if (slot == toybox::kDisplayFont) return toybox::kDisplayCut;
  if (slot == toybox::kUiFont) return toybox::kUiCut;
  return toybox::kTileCut;
}

// The band a recorded run puts ink in. Wrapped runs keep their rect: the target
// lays those out by the block, and this rule is the single-line one.
fui::Rect inkBandOf(const FakeTarget::TextRun& run) {
  if (run.style.maxLines > 1) return run.rect;
  const toybox::CutMetrics& cut = cutForSlot(run.style.font);
  return fui::makeRect(run.rect.x, static_cast<int16_t>(inkTopIn(run.rect, cut)), run.rect.width, cut.inkHeight);
}

// The X4 Pro's logical frame.
fui::DeviceContext device() {
  fui::DeviceContext ctx;
  ctx.width = 480;
  ctx.height = 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

// --- the chrome probe ------------------------------------------------------
//
// What Mario reported twice: content sitting on the header. The header work
// that answered it both times fixed the HEADER, and the header was never the
// half that was wrong -- every screen decides for itself where its content
// starts, and a dozen of them decided it from toybox::kHeaderHeight, a constant
// that names the black band and knows nothing about the rule drawn under it.
//
// So this is not a per-screen test. A per-screen test is the thing that failed:
// it covers the screen you thought of, and Mario opens the other one. It lives
// in ~Rendered, so EVERY screen this suite renders is measured -- including the
// ones written after this comment by someone who never read it.
//
// The rule: the chrome owns rows 0..kChromeHeight (the band, the gap, the
// rule), and the first content pixel below it clears kGutter. That is the same
// number card #295 gave the Yahtzee dice, so this is the fork's own answer to
// "how far must content clear the header" applied everywhere rather than once.
// A screen may still draw INSIDE the band -- folder marks, medal tallies, face
// doors -- and those are placed by bandCenterY()/headerInkRect() on purpose.
//
// Text is measured as INK, not as its line box. A run's rect is the box the
// text was given and the glyphs sit inset within it (see inkTopIn), so
// measuring the rect would report collisions the eye cannot see and move type
// that already clears.
//
// KNOWN UNDERSTATEMENT, and it points the wrong way: inkBandOf resolves a cut
// through cutForSlot, which knows the three Jersey cuts and nothing else. The
// readers rebind their slots -- readingChromeFaces() puts the UI cut in the
// SMALL slot -- so a SMALL-slot run in reader chrome is measured against
// kTileCut when kUiCut drew it, and the probe puts its ink about five pixels
// LOWER than the truth. Nothing is close enough for that to matter today
// (Hacker News and Instapaper start 24px clear of the floor), but the margin is
// what protects them, not this check. A probe that resolved the cut from the
// theme actually in force would close it.

// The band a render actually painted, or 0. Taken from the paint rather than
// from kHeaderHeight, because the band is a THEME token and Solitaire raises
// it: a probe keyed to the constant would measure that screen against a line
// seven rows from where its rule is, and would have to be told to skip it --
// which is how a screen ends up outside the only check that would have caught
// it. headerBand() paints one full-width rect at row 0 and nothing else does.
fui::Rect bandRectOf(const FakeTarget& t) {
  fui::Rect band{};
  for (size_t i = 0; i < t.fills.size(); ++i) {
    const fui::Rect& r = t.fills[i];
    // Full-bleed from the panel's top-left corner. The width is not asserted
    // against 480: Solitaire is landscape, and a probe that assumed portrait
    // would silently stop looking at the one app whose band is not standard.
    if (r.x != 0 || r.y != 0 || r.width < 480 || r.height <= 0) continue;
    if (r.height > toybox::kHeaderHeight) continue;
    // And its RULE. A black strip at row 0 is not on its own a header: the
    // Forehead round screen paints one across each long edge to label the two
    // physical keys, and it has no header at all. headerBand() draws the band
    // and the rule together, so the pair is the signature and a lone strip is
    // not.
    bool ruled = false;
    for (size_t j = 0; j < t.fills.size(); ++j) {
      const fui::Rect& q = t.fills[j];
      if (q.x == 0 && q.width == r.width && q.height == toybox::kRule && q.y == r.height + toybox::kBandRuleGap) {
        ruled = true;
        break;
      }
    }
    if (!ruled) continue;
    if (r.height > band.height) band = r;
  }
  return band;
}

// The first row below the chrome that content may use.
int16_t chromeFloorFor(const fui::Rect& band) {
  return static_cast<int16_t>(band.height + toybox::kBandRuleGap + toybox::kRule + toybox::kGutter);
}

// True for the two rects headerBand() itself paints, which are allowed to be
// exactly where they are and nowhere else.
bool isChromePaint(const fui::Rect& r, const fui::Rect& band) {
  if (r.x != 0 || r.width != band.width) return false;
  if (r.y == 0 && r.height == band.height) return true;                                     // the band
  if (r.y == band.height + toybox::kBandRuleGap && r.height == toybox::kRule) return true;  // the rule
  return false;
}

// Anything wholly inside the band is band ink, and belongs there.
bool insideBand(const fui::Rect& r, const fui::Rect& band) { return r.bottom() <= band.height; }

struct ChromeHit {
  fui::Rect rect{};
  std::string what;
  bool found = false;
};

void noteHit(ChromeHit& hit, const fui::Rect& r, const std::string& what) {
  if (hit.found && hit.rect.y <= r.y) return;
  hit = ChromeHit{r, what, true};
}

// The topmost thing that fails to clear the chrome, or nothing.
ChromeHit chromeIntrusion(const FakeTarget& t, const fui::Rect& band) {
  ChromeHit hit;
  const int16_t floor = chromeFloorFor(band);
  for (size_t i = 0; i < t.fills.size(); ++i) {
    const fui::Rect& r = t.fills[i];
    if (isChromePaint(r, band) || insideBand(r, band) || r.height <= 0 || r.width <= 0) continue;
    if (r.y < floor) noteHit(hit, r, "fill");
  }
  for (const auto& run : t.texts) {
    const fui::Rect ink = inkBandOf(run);
    if (insideBand(ink, band)) continue;
    if (ink.y < floor) noteHit(hit, ink, "text \"" + run.text + "\"");
  }
  for (const auto& blit : t.blits) {
    if (insideBand(blit.rect, band)) continue;
    if (blit.rect.y < floor) noteHit(hit, blit.rect, "bitmap");
  }
  for (const auto& st : t.strokes) {
    if (insideBand(st.rect, band)) continue;
    if (st.rect.y < floor) noteHit(hit, st.rect, "stroke");
  }
  // Triangles too. FakeTarget records them and this walk used to skip them, so
  // a chevron or a pointer in the chrome's rows was invisible -- and five apps
  // draw with them (insider, connections, jaipur, forehead, toy battle). A
  // probe that reads four of the five op kinds reports clean on the fifth.
  for (const auto& tri : t.triangles) {
    const int16_t top =
        tri.a.y < tri.b.y ? (tri.a.y < tri.c.y ? tri.a.y : tri.c.y) : (tri.b.y < tri.c.y ? tri.b.y : tri.c.y);
    const int16_t bottom =
        tri.a.y > tri.b.y ? (tri.a.y > tri.c.y ? tri.a.y : tri.c.y) : (tri.b.y > tri.c.y ? tri.b.y : tri.c.y);
    if (bottom <= band.height) continue;
    if (top < floor) noteHit(hit, fui::makeRect(0, top, 1, static_cast<int16_t>(bottom - top)), "triangle");
  }
  return hit;
}

// The title, so a failure says which screen without anyone having to guess.
// How many renders the probe actually measured, and how many it passed over.
// Printed at the end of the run: a probe whose coverage silently drops to zero
// reports exactly what a clean tree reports.
int chromeScreensMeasured = 0;
int chromeScreensSkipped = 0;

std::string bandLabel(const FakeTarget& t, const fui::Rect& band) {
  for (const auto& run : t.texts) {
    if (insideBand(inkBandOf(run), band)) return run.text;
  }
  return "?";
}

// One rendered screen, with everything the assertions need to inspect.
struct Rendered {
  FakeTarget target;
  toybox::Interactions interactions;
  // One per rendered screen, which is what an Activity holds: a reader keeps
  // its wrap between paints, so a test that wants to ask "did the second paint
  // wrap again" has to reuse the same Rendered.
  toybox::WrappedText wrap;
  // The words themselves, which used to ride on the model. Kept here so the
  // helpers below hand the SAME pointer and style to the counting and the
  // drawing, which is the whole point of bundling them. Defaulted to a
  // sentence that wraps, so the reader tests that only care about the chrome
  // still have a body to draw.
  const char* bodyText = "Some words that go on for a while and wrap onto more than one line of the panel.";

  // Whether the paint registered any control carrying this action. Asking the
  // TABLE rather than the pixels is what separates "the button is drawn" from
  // "the button can be tapped", and the trash square on Go's front door is one
  // control where the two came apart: it is drawn over a list row that was
  // registered at full width, so the hit test decides which one wins.
  bool has(const fui::ActionId action) const {
    for (size_t i = 0; i < interactions.count(); ++i) {
      if (interactions.data()[i].action == action) return true;
    }
    return false;
  }

  // Routes a tap at logical (x, y) against what was just drawn, which is the
  // whole point: the table under test is the one the paint produced.
  fui::ActionEvent tap(const int x, const int y) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    return interactions.route(input);
  }

  // Measured on the way out, so no test has to remember to ask. See the chrome
  // probe above for why this is not a per-screen assertion.
  ~Rendered() {
    const fui::Rect band = bandRectOf(target);
    if (band.height == 0) {
      // Not a header render: a popup, a headerless play screen (Forehead's
      // round, every Wavelength screen), or a build that drew nothing. Counted
      // rather than ignored, because "measured and clean" and "never looked at"
      // are the same silence otherwise.
      ++chromeScreensSkipped;
      return;
    }
    ++chromeScreensMeasured;
    const ChromeHit hit = chromeIntrusion(target, band);
    if (hit.found) {
      std::printf("FAIL chrome: [%s] %s at y=%d clears the %dpx band's rule by %d, needs %d\n",
                  bandLabel(target, band).c_str(), hit.what.c_str(), static_cast<int>(hit.rect.y),
                  static_cast<int>(band.height),
                  static_cast<int>(hit.rect.y - band.height - toybox::kBandRuleGap - toybox::kRule), toybox::kGutter);
    }
    check(!hit.found, "content clears the header chrome by a gutter", __LINE__);
  }
};

// Present is not the same as legible. drewText() sees the string the builder
// HANDED the renderer, and the renderer is what shortens it -- so a button
// whose box is too narrow for its own label passes every "did it draw?" check
// while the panel says "UNDO A...". This asks the target to measure the run it
// recorded against the rect it was given, which is the one comparison the
// truncation is decided by.
bool drewLabelWhole(const Rendered& out, const char* needle) {
  bool found = false;
  for (const auto& run : out.target.texts) {
    if (run.text != needle) continue;
    found = true;
    if (out.target.measureText(run.style.font, run.text.c_str(), run.style).width > run.rect.width) return false;
  }
  return found;
}

// The height this text needs with the LINE CAP LIFTED, against the width it was
// drawn into.
//
// Measuring with the run's own style is a tautology wherever the builder sized
// the rect from that same call: the check restates the line it is guarding and
// can only fail if that line disappears entirely. Worse, it is blind to the
// mechanism it exists to catch. layoutText clamps to style.maxLines and
// ellipsizes whatever is left over, so a wording that needs five lines under a
// four-line cap is silently cut, the capped measure dutifully reports four, and
// the reserved rect matches it exactly.
//
// style.maxLines saturates at layoutText's own MAX_LINES (16), so asking for 16
// is asking for as many lines as the sentence takes. Comparing THAT against the
// reserved rect is the comparison the truncation is actually decided by.
int16_t uncappedWrappedHeight(const FakeTarget& target, const FakeTarget::TextRun& run) {
  fui::TextStyle uncapped = run.style;
  uncapped.maxLines = 16;
  return fui::measureWrappedText(target, run.text.c_str(), uncapped, run.rect.width).height;
}

void buildLink(Rendered& out, const linkui::LinkModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  linkui::buildLink(screen, model);
}

// --- the shared multiplayer screen -----------------------------------------

linkui::LinkModel searchingModel() {
  linkui::LinkModel model;
  model.gameTitle = "BATTLESHIP";
  model.headline = "LOOKING FOR A PLAYER";
  model.yourName = "MARIO";
  model.you = linkui::SeatState::Ready;
  model.them = linkui::SeatState::Looking;
  return model;
}

void testSearchingAsksNothing() {
  // The whole claim: tap MULTIPLAYER and the only thing on screen is that it is
  // looking. No device list, no host/join, no pairing code, no retry.
  Rendered out;
  buildLink(out, searchingModel());

  CHECK(out.target.drew("BATTLESHIP"));
  CHECK(out.target.drew("LOOKING FOR A PLAYER"));
  CHECK(out.target.drew("MARIO"));
  // The empty seat shows the shape of the absence rather than a blank row.
  CHECK(out.target.drew("- - - -"));
  CHECK(out.target.drew("LOOKING"));

  // One control, and it is the way out rather than a choice. PLAY AGAIN has no
  // business here: there is no game yet to play again.
  CHECK(out.target.drew("BACK"));
  CHECK(!out.target.drew("PLAY AGAIN"));
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back != nullptr) {
    const fui::ActionEvent event = out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2);
    CHECK(event.action == linkui::ActionLeaveLink);
  }
  // The seats are not buttons: a tap on one must do nothing, or a mis-drawn hit
  // region would hand the player a control that cannot work.
  const FakeTarget::TextRun* seat = out.target.find("MARIO");
  CHECK(seat != nullptr);
  if (seat != nullptr) {
    const fui::ActionEvent event = out.tap(seat->rect.x + seat->rect.width / 2, seat->rect.y + seat->rect.height / 2);
    CHECK(event.action == fui::NO_ACTION);
  }
}

void testSeatsSayWhatEachPlayerHasDecided() {
  // The rematch used to be a guess: you tapped PLAY AGAIN and stared at a
  // screen that could not tell you whether they had. Each seat now reads out.
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Looking, false), "LOOKING") == 0);
  // Once linked, an empty-handed seat is waiting on a person, not on a search.
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Looking, true), "WAITING") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Deciding, true), "DECIDING") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Ready, true), "READY") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Left, true), "LEFT") == 0);
  CHECK(strcmp(linkui::seatValue(linkui::SeatState::Lost, true), "LOST") == 0);
}

void testTheRematchShowsBothAnswers() {
  Rendered out;
  linkui::LinkModel model;
  model.gameTitle = "BATTLESHIP";
  model.headline = "CHECKMATE";
  model.yourName = "MARIO";
  model.theirName = "LUIGI";
  model.you = linkui::SeatState::Ready;
  model.them = linkui::SeatState::Deciding;
  model.linked = true;
  model.offerPlayAgain = true;
  buildLink(out, model);

  CHECK(out.target.drew("CHECKMATE"));
  CHECK(out.target.drew("MARIO"));
  CHECK(out.target.drew("LUIGI"));
  // You have answered and they have not, and the screen says exactly that.
  CHECK(out.target.drew("READY"));
  CHECK(out.target.drew("DECIDING"));

  CHECK(out.target.drew("PLAY AGAIN"));
  const FakeTarget::TextRun* again = out.target.find("PLAY AGAIN");
  CHECK(again != nullptr);
  if (again != nullptr) {
    const fui::ActionEvent event =
        out.tap(again->rect.x + again->rect.width / 2, again->rect.y + again->rect.height / 2);
    CHECK(event.action == linkui::ActionPlayAgain);
  }
  // Stacked pills must not share a hit band.
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back != nullptr) {
    const fui::ActionEvent event = out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2);
    CHECK(event.action == linkui::ActionLeaveLink);
  }
}

// The bottom band is not a style choice, and this is the assertion that says so.
//
// y = 800 - kMargin - kPillHeight = 732 is where every link game's board puts
// the status capsule that becomes PLAY AGAIN at game over. This screen replaces
// that board in the same pass that ends the game, with no announcement and no
// settle, so whatever occupies 732 is what a thumb already on its way there
// hits. LEAVE used to be it: the rematch tap killed the radio instead.
//
// Asserted as "the destructive action is nowhere in the band" rather than as a
// literal rect, so a layout change that moves BACK back down fails here even if
// it moves it by a different arithmetic.
void testTheRematchBandIsNotTheWayOut() {
  Rendered out;
  linkui::LinkModel model;
  model.gameTitle = "BATTLESHIP";
  model.headline = "CHECKMATE";
  model.yourName = "YOU";
  model.theirName = "LUIGI";
  model.you = linkui::SeatState::Deciding;
  model.them = linkui::SeatState::Deciding;
  model.linked = true;
  model.offerPlayAgain = true;
  buildLink(out, model);

  // The band the boards hand over: the full pill, at the full content width.
  const int bandTop = 800 - toybox::kMargin - toybox::kPillHeight;
  const int bandBottom = 800 - toybox::kMargin;
  for (int y = bandTop; y < bandBottom; y += 4) {
    for (int x = toybox::kMargin; x < 480 - toybox::kMargin; x += 16) {
      const fui::ActionEvent event = out.tap(x, y);
      CHECK(event.action != linkui::ActionLeaveLink);
      CHECK(event.action == linkui::ActionPlayAgain);
    }
  }
  // Battleship's capsule is inset by the opponent face, so its own game-over
  // PLAY AGAIN starts at x=76. That exact pixel must not leave the match.
  CHECK(out.tap(76 + 4, bandTop + toybox::kPillHeight / 2).action == linkui::ActionPlayAgain);

  // And BACK is still reachable, one row up, where no board draws a control.
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back != nullptr) {
    CHECK(back->rect.y < bandTop);
    const fui::ActionEvent event = out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2);
    CHECK(event.action == linkui::ActionLeaveLink);
  }

  // The two pills must not share a pixel: a leave that overlaps the rematch by
  // one row is the same bug wearing a smaller number.
  const FakeTarget::TextRun* again = out.target.find("PLAY AGAIN");
  CHECK(again != nullptr);
  if (again != nullptr && back != nullptr) {
    CHECK(back->rect.y + back->rect.height <= again->rect.y);
  }
}

// Alone, BACK keeps the bottom band. Nothing is at risk there -- the only
// screens that reach this state are the search (which replaces a menu) and an
// opponent who has already gone -- and a single pill floating one row up over
// an empty margin reads as a layout that lost something.
void testTheLoneWayOutKeepsTheBottomBand() {
  Rendered out;
  buildLink(out, searchingModel());
  const int bandMid = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(240, bandMid).action == linkui::ActionLeaveLink);
}

void testAnOpponentWhoHasGoneTakesTheButtonWithThem() {
  // A button that cannot work is worse than one that is not there.
  Rendered out;
  linkui::LinkModel model;
  model.gameTitle = "BATTLESHIP";
  model.headline = "LUIGI LEFT";
  model.yourName = "MARIO";
  model.theirName = "LUIGI";
  model.you = linkui::SeatState::Deciding;
  model.them = linkui::SeatState::Left;
  model.linked = true;
  model.offerPlayAgain = false;
  buildLink(out, model);

  CHECK(out.target.drew("LUIGI LEFT"));
  CHECK(out.target.drew("LEFT"));
  CHECK(!out.target.drew("PLAY AGAIN"));
  CHECK(out.target.drew("BACK"));
}

// --- Solitaire ------------------------------------------------------------
//
// The one app the ui suite COMPILED and never rendered. That is not a gap in
// its own tests -- host-tests/solitaire covers the rules -- it is a gap in this
// file's chrome probe, which measures whatever is rendered here and therefore
// measured nothing at all for the only landscape screen in the fork and the
// only one that raises its header band. Card #248 found it by asking which
// screens the probe had actually seen, which a green run does not say.
//
// Three renders, because Solitaire has three bands and the fault it had -- a
// top row nine pixels under the rule, plus a rule drawn a second time by hand
// on top of the one headerBand() draws -- was on all three.
fui::DeviceContext solitaireDevice() {
  fui::DeviceContext ctx;
  ctx.width = 800;
  ctx.height = 480;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

toybox::Screen solitaireScreen(toybox::Frame& frame, fui::ThemeTokens& tokens) {
  tokens = toybox::themeTokens();
  tokens.headerHeight = solitaireui::kHeaderBand;
  return toybox::Screen(frame, tokens);
}

void solitaireDrawsOneRuleAndClearsIt() {
  const fui::DeviceContext ctx = solitaireDevice();
  const fui::InputSnapshot noInput{};
  solitaire::Game game;
  game.deal(12345, false);

  // The board. ~Rendered measures the clearance; what is asserted here is the
  // half a clearance check cannot see: exactly ONE rule under the band.
  Rendered board;
  {
    toybox::Frame frame(board.target, ctx, noInput, board.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = solitaireScreen(frame, tokens);
    solitaireui::BoardModel model;
    model.game = &game;
    solitaireui::Layout layout;
    solitaireui::buildBoard(screen, model, layout);
  }
  int rules = 0;
  for (size_t i = 0; i < board.target.fills.size(); ++i) {
    const fui::Rect& r = board.target.fills[i];
    if (r.x == 0 && r.width == ctx.width && r.height == toybox::kRule &&
        r.y == solitaireui::kHeaderBand + toybox::kBandRuleGap) {
      ++rules;
    }
  }
  // Two, until this card: headerBand() draws the rule for every screen in the
  // fork, and this app kept drawing its own on the same pixels. Identical ink,
  // so nothing looked wrong -- which is the point. A second copy of the
  // chrome's geometry in an app file is a bug that is waiting rather than a bug
  // that is showing.
  CHECK(rules == 1);
  // And the probe recognised this band, which is a 56px one. A render it does
  // not recognise is a render it silently skips.
  CHECK(bandRectOf(board.target).height == solitaireui::kHeaderBand);

  Rendered menu;
  {
    toybox::Frame frame(menu.target, ctx, noInput, menu.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = solitaireScreen(frame, tokens);
    solitaireui::MenuModel model;
    model.hasSave = true;
    model.savedMoves = 42;
    model.played = 9;
    model.wins = 3;
    model.streak = 1;
    solitaireui::buildMenu(screen, model);
  }
  CHECK(menu.target.drew("SOLITAIRE"));
  CHECK(bandRectOf(menu.target).height == solitaireui::kHeaderBand);

  Rendered win;
  {
    toybox::Frame frame(win.target, ctx, noInput, win.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = solitaireScreen(frame, tokens);
    solitaireui::WinModel model;
    model.moves = 120;
    model.wins = 4;
    model.streak = 2;
    solitaireui::buildWin(screen, model);
  }
  CHECK(win.target.texts.size() > 0);
  CHECK(bandRectOf(win.target).height == solitaireui::kHeaderBand);

  // And the band this app raises is the number the Activity hands the theme.
  // It was 56 typed twice in two files; the builders and the token could
  // disagree and nothing would say so.
  CHECK(solitaireui::kHeaderBand == 56);
}

// The probe, tested. Every op kind FakeTarget records is planted one pixel
// inside the chrome's forbidden rows and must be caught.
//
// This exists because the probe shipped blind to triangles: it walked fills,
// texts, blits and strokes, and five apps draw with triangles. Nothing failed,
// which is what being blind looks like. A check whose own failure has never
// been demonstrated is a check nobody has tested, and the fork has paid for
// that distinction more than once -- so each kind is asserted to be SEEN here,
// rather than the whole probe being asserted to be clean somewhere else.
void theChromeProbeCatchesEveryDrawKind() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  // One row below the band's bottom, which is inside the gap the rule sits in
  // and well inside the gutter every screen must clear.
  const int16_t inside = static_cast<int16_t>(toybox::kHeaderHeight + 1);

  struct Case {
    const char* what;
    void (*draw)(toybox::Screen&, int16_t);
  };
  const Case cases[] = {
      {"fill",
       [](toybox::Screen& screen, const int16_t y) {
         screen.target().fill(fui::makeRect(10, y, 40, 20), fui::Paint::solid(fui::Color::Black));
       }},
      {"text",
       [](toybox::Screen& screen, const int16_t y) {
         fui::TextStyle style;
         style.font = toybox::kUiFont;
         screen.target().text(fui::makeRect(10, y, 200, 40), "TOO HIGH", style);
       }},
      {"bitmap",
       [](toybox::Screen& screen, const int16_t y) {
         screen.target().bitmap(fui::makeRect(10, y, 32, 32), fui::bitmapFromIcon(icon_saved_32),
                                fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
       }},
      {"stroke",
       [](toybox::Screen& screen, const int16_t y) {
         screen.target().stroke(fui::makeRect(10, y, 40, 20), fui::Paint::solid(fui::Color::Black), 2);
       }},
      {"triangle",
       [](toybox::Screen& screen, const int16_t y) {
         screen.target().triangle(fui::Point{10, y}, fui::Point{50, y}, fui::Point{30, static_cast<int16_t>(y + 20)},
                                  fui::Paint::solid(fui::Color::Black));
       }},
  };

  for (const Case& c : cases) {
    FakeTarget target;
    toybox::Interactions interactions;
    toybox::Frame frame(target, ctx, noInput, interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    fui::HeaderProps props;
    props.title = "TITLE";
    toybox::headerBand(screen, props);
    c.draw(screen, inside);

    const fui::Rect band = bandRectOf(target);
    CHECK(band.height == toybox::kHeaderHeight);
    const ChromeHit hit = chromeIntrusion(target, band);
    if (!hit.found) std::printf("FAIL the chrome probe is blind to a %s\n", c.what);
    CHECK(hit.found);
  }

  // And the same five, placed a gutter below the chrome, are NOT caught. A
  // probe that flagged everything would pass the loop above and be useless.
  for (const Case& c : cases) {
    FakeTarget target;
    toybox::Interactions interactions;
    toybox::Frame frame(target, ctx, noInput, interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    fui::HeaderProps props;
    props.title = "TITLE";
    toybox::headerBand(screen, props);
    // Well clear: the text case is measured as ink, which sits lower than its
    // box, so the box itself starting at the floor is the tightest legal case.
    c.draw(screen, static_cast<int16_t>(toybox::kChromeHeight + toybox::kGutter));

    const ChromeHit hit = chromeIntrusion(target, bandRectOf(target));
    if (hit.found) std::printf("FAIL the chrome probe flags a legal %s at the floor\n", c.what);
    CHECK(!hit.found);
  }
}

void everyBandCarriesItsRule() {
  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  fui::HeaderProps props;
  props.title = "TITLE";
  toybox::headerBand(screen, props);

  // Half one: the chrome reserves every row it paints. The band's black stops
  // at kHeaderHeight and the rule is drawn kBandRuleGap below that, so the
  // first row a screen owns is kChromeHeight -- and screen.body().y says so.
  //
  // This assertion used to read `== kHeaderHeight`, on the reasoning that no
  // screen's content should move to buy the rule. That was true of the rule
  // and false of the body: it left body().y pointing at the top of a line the
  // header had already drawn, so the honest way of laying out a screen -- take
  // the body and add a gutter -- put content five pixels under the rule. The
  // Connections calendar and the Wallpapers grid both did precisely that, and
  // both are correct now without either file being touched, which is the only
  // kind of fix that survives the next twenty screens. See card #248.
  CHECK(screen.body().y == toybox::kChromeHeight);
  CHECK(screen.body().y == toybox::kHeaderHeight + toybox::kBandRuleGap + toybox::kRule);

  // Half two: a black, full-bleed rule, kBandRuleGap below the band.
  bool ruled = false;
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Rect& r = out.target.fills[i];
    const fui::Paint& paint = out.target.fillPaints[i];
    if (paint.kind != fui::PaintKind::Solid || paint.color != fui::Color::Black) continue;
    if (r.y != toybox::kHeaderHeight + toybox::kBandRuleGap || r.height != toybox::kRule) continue;
    if (r.x == 0 && r.width == ctx.screen().width) ruled = true;
  }
  CHECK(ruled);

  // The band's own black must still reach kHeaderHeight, or the rule is not a
  // rule under a band -- it is a stripe in a gap. This is the assertion that
  // fails on the arrangement tried first, which carved the gap and rule out of
  // the header's height: that shortened the band to 70, tripped the vertical
  // clamp on the title's line box, and stopped the header looking centred
  // behind the X4 Pro's bezel.
  bool bandFull = false;
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Rect& r = out.target.fills[i];
    if (r.y == 0 && r.height == toybox::kHeaderHeight && r.width == ctx.screen().width) bandFull = true;
  }
  CHECK(bandFull);

  // headerRule() is a no-op now. 27 call sites still name it, and if it drew
  // anything they would each paint a SECOND line down in the body.
  const size_t before = out.target.fills.size();
  toybox::headerRule(screen);
  CHECK(out.target.fills.size() == before);
}

// --- battleship -------------------------------------------------------------

void buildBattleshipStart(Rendered& out, const bshipui::StartModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  bshipui::buildStartMenu(screen, model);
}

void buildBattleshipBoard(Rendered& out, const bshipui::BoardModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  bshipui::buildBoardChrome(screen, model);
}

void buildBattleshipPlace(Rendered& out, const bshipui::PlaceModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  bshipui::buildPlaceChrome(screen, model);
}

// The paint clock is one global counter, and every other test in this file
// runs with it at zero -- which is exactly the "nothing has been shown yet"
// state that leaves the gate open. A test that advances it therefore has to
// put it back, or it silently gates the ~600 tests that come after it.
struct PaintClockGuard {
  uint32_t saved = paintclock::counter();
  ~PaintClockGuard() { paintclock::counter() = saved; }
};

// The one this whole mechanism exists for.
//
// BattleshipScreens.cpp:160 registers the bottom capsule as
//     gameOver ? ActionPlayAgain : (canFire ? ActionFire : NO_ACTION)
// so one rect means FIRE for the whole game and becomes PLAY AGAIN the instant
// the last shot lands. FIRE is tapped dozens of times a game; the player's
// thumb lives on that pixel. The rebuild happens BEFORE displayBuffer(), which
// blocks 0.3-2s, so without a gate the capsule is already PLAY AGAIN while the
// panel still reads FIRE.
//
// This drives the real builder through that exact sequence, and it is written
// so that "the capsule is live over a frame that still says FIRE" cannot pass.
void testACapsuleThatChangedMeaningWaitsForThePanel() {
  PaintClockGuard clock;
  Rendered out;

  bshipui::BoardModel firing;
  firing.status = "FIRE";
  firing.canFire = true;
  firing.theirName = "LUIGI";

  // Mid-game: the board is built and the panel has shown it.
  buildBattleshipBoard(out, firing);
  paintclock::notePainted();

  // The capsule, in the bottom band. x=300 is inside it whether or not the
  // opponent's face has taken the left strip.
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  // The last shot lands. The activity rebuilds and has NOT painted yet: this
  // is the window, and the panel is still showing FIRE.
  bshipui::BoardModel over = firing;
  over.canFire = false;
  over.gameOver = true;
  over.status = "PLAY AGAIN";
  buildBattleshipBoard(out, over);

  // The rect now says PLAY AGAIN in the table. A thumb already travelling to
  // FIRE must get nothing at all -- not FIRE (the game is over) and above all
  // not PLAY AGAIN (a rematch nobody asked for).
  const fui::ActionEvent duringPaint = out.tap(300, capsuleY);
  CHECK(duringPaint.action != bshipui::ActionPlayAgain);
  CHECK(duringPaint.action != bshipui::ActionFire);
  CHECK(duringPaint.action == fui::NO_ACTION);
  CHECK(!out.interactions.routable());

  // The panel catches up. From here the control is real and answers.
  paintclock::notePainted();
  CHECK(out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionPlayAgain);
}

// The other half, and the one that decides whether this fix is worth having:
// input must NOT go dead. MappedInputManager::rowTouch() reports Down after
// 90ms of contact, apps repaint to highlight the row, and then act on the
// RELEASE of that same contact. That repaint rebuilds an identical table while
// the finger is still down, so if an ordinary repaint gated the release, every
// list in the fork would highlight and then do nothing.
void testARepaintThatChangedNothingStillAnswers() {
  PaintClockGuard clock;
  Rendered out;

  bshipui::BoardModel firing;
  firing.status = "FIRE";
  firing.canFire = true;

  buildBattleshipBoard(out, firing);
  paintclock::notePainted();
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  // Rebuilt with the same model, mid-contact, with no paint since. Same table,
  // same meaning: the release still has to land.
  buildBattleshipBoard(out, firing);
  CHECK(out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  // And repeatedly, because a highlight can repaint several times before the
  // finger lifts. Nothing here may accumulate into a closed gate.
  for (int repaint = 0; repaint < 5; ++repaint) {
    buildBattleshipBoard(out, firing);
    CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);
  }
}

// A changed screen that is rebuilt again before it is ever painted must stay
// gated. The panel is still showing the table from two builds ago, so adopting
// the intermediate one as "shown" would reopen the gate on a frame nobody saw.
void testAnUnshownRebuildDoesNotCountAsShown() {
  PaintClockGuard clock;
  Rendered out;

  bshipui::BoardModel firing;
  firing.status = "FIRE";
  firing.canFire = true;
  buildBattleshipBoard(out, firing);
  paintclock::notePainted();
  const int capsuleY = 800 - toybox::kMargin - toybox::kPillHeight / 2;
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionFire);

  bshipui::BoardModel over = firing;
  over.canFire = false;
  over.gameOver = true;
  buildBattleshipBoard(out, over);
  CHECK(!out.interactions.routable());

  // Rebuilt again, still unpainted. FIRE is what the panel shows and PLAY
  // AGAIN is what the table says; the gate stays shut.
  buildBattleshipBoard(out, over);
  CHECK(!out.interactions.routable());
  CHECK(out.tap(300, capsuleY).action == fui::NO_ACTION);

  // One paint is all it takes to open, and it opens fully.
  paintclock::notePainted();
  CHECK(out.tap(300, capsuleY).action == bshipui::ActionPlayAgain);
}

// paintclock::RevealGate is the same decision UiAppHost makes, lifted out so
// it can be tested: UiAppHost needs a GfxRenderer and an Arduino and cannot be
// built here, and a restatement of its logic in a test would only ever agree
// with itself. This exercises the object the firmware actually uses.
void testTheRevealGateWaitsForOnePaintAndThenLatches() {
  PaintClockGuard clock;
  paintclock::RevealGate gate;

  // Unarmed: never in the way.
  CHECK(gate.revealed());

  // A screen entry. Built, but the panel still shows the previous screen.
  gate.arm();
  gate.markBuilt();
  CHECK(!gate.revealed());

  // A render that rebuilds several times before its single paint (which is
  // what UiListActivity does, up to 8 passes) must measure from the LAST
  // build, or the gate opens on a paint that predates the table.
  paintclock::notePainted();
  gate.markBuilt();
  CHECK(!gate.revealed());

  // One paint from any source releases it.
  paintclock::notePainted();
  CHECK(gate.revealed());

  // And it latches: a later build with no arm() must not re-close it, or an
  // ordinary repaint would start eating input.
  gate.markBuilt();
  CHECK(gate.revealed());
  CHECK(gate.revealed());

  // Only a fresh screen entry closes it again.
  gate.arm();
  gate.markBuilt();
  CHECK(!gate.revealed());
  paintclock::notePainted();
  CHECK(gate.revealed());
}

// The eight games that hit-test a board against GEOMETRY never reach route(),
// so no table digest can see their taps -- and what such a tap MEANS is not in
// the table either. MinesweeperScreens.cpp registers the FLAG capsule with an
// identical rect, action, value and inputMask and flips only StateSelected,
// which the digest ignores as paint, while that same mode bit decides whether
// a grid tap digs or flags. paintclock::SurfaceGate is the decision those apps
// make instead; this drives the object the firmware uses, not a restatement.
void testTheSurfaceGateHoldsAChangedMeaningAndPassesAnUnchangedOne() {
  PaintClockGuard clock;
  paintclock::SurfaceGate gate;

  // Before the first paint of all there is no shown frame to disagree with,
  // so nothing is gated -- the boot splash window, and every other test here.
  CHECK(gate.routable(0));
  CHECK(gate.routable(12345));

  // Minesweeper, DIG mode, on the panel.
  const uint32_t dig = 0;
  const uint32_t flag = 1;
  gate.noteBuilt(dig);
  paintclock::notePainted();
  CHECK(gate.routable(dig));

  // The FLAG capsule is tapped: flagMode flips and the board is rebuilt. The
  // panel still reads DIG for the length of the refresh, so a grid tap in this
  // window must NOT flag.
  gate.noteBuilt(flag);
  CHECK(!gate.routable(flag));

  // The refresh completes. The panel now reads FLAG and the board is live.
  paintclock::notePainted();
  CHECK(gate.routable(flag));

  // THE safety property, and the reason this is a digest rather than a
  // suppression: a repaint that changed nothing still answers. Minesweeper
  // holds a finger on a cell, requestUpdate() repaints the outline, and the
  // LIFT of that same contact is what digs. Gating it would eat the move and
  // read as a frozen device.
  gate.noteBuilt(flag);
  CHECK(gate.routable(flag));
  gate.noteBuilt(flag);
  CHECK(gate.routable(flag));

  // Back to DIG on the panel, so the next block measures from a known frame.
  paintclock::notePainted();
  gate.noteBuilt(dig);
  CHECK(!gate.routable(dig));
  paintclock::notePainted();
  CHECK(gate.routable(dig));

  // A render that rebuilds several times before its single paint must measure
  // from the frame the panel last SHOWED, not from an intermediate build the
  // user never saw. Two builds, no paint between: the gate stays shut against
  // the meaning that ends up built...
  const uint32_t pencil = 2;
  gate.noteBuilt(flag);
  gate.noteBuilt(pencil);
  CHECK(!gate.routable(pencil));
  // ...and open against the one still on the glass, which is DIG and not the
  // intermediate FLAG build. Taking the intermediate as "shown" is the bug
  // this check exists to catch.
  CHECK(gate.routable(dig));
  CHECK(!gate.routable(flag));

  paintclock::notePainted();
  CHECK(gate.routable(pencil));
}

// Several small values fold into one meaning, and they must not collide when
// they swap places: "selected e2, white to move" is not "selected d4, black to
// move".
void testMeaningsMixPositionally() {
  const uint32_t a = paintclock::mixMeaning(paintclock::mixMeaning(paintclock::kMeaningSeed, 4), 7);
  const uint32_t b = paintclock::mixMeaning(paintclock::mixMeaning(paintclock::kMeaningSeed, 7), 4);
  CHECK(a != b);
  const uint32_t again = paintclock::mixMeaning(paintclock::mixMeaning(paintclock::kMeaningSeed, 4), 7);
  CHECK(a == again);
}

// OptionPopup and KeyboardEntryActivity hold their own buffers at their own
// capacities (17 and 48) and opt into the SDK's double-buffered publish cycle,
// which the 24-slot toybox screens do not. beginBuild() therefore has to
// digest the PUBLISHED generation: by the time a publishing caller builds,
// building_ has already flipped and data() is a rebuild from two generations
// ago, which would be compared against as though the panel had shown it.
void testAPublishingBufferDigestsWhatThePanelIsShowing() {
  PaintClockGuard clock;
  paintclock::RevealedInteractions<17> iact;
  freeink::ui::InteractionBuffer<17>& raw = iact;

  const auto slot = [](const freeink::ui::ActionId action, const int16_t value) {
    freeink::ui::Interaction hit{};
    hit.rect = fui::Rect{0, 0, 100, 40};
    hit.action = action;
    hit.value = value;
    hit.inputMask = fui::InputTouch;
    return hit;
  };
  const auto tap = [&iact]() {
    fui::InputSnapshot in{};
    in.touchReleased = true;
    in.touchX = 10;
    in.touchY = 10;
    return iact.routePublished(in);
  };

  // A popup is shown and the panel catches up.
  iact.beginBuild();
  iact.beginPublishCycle();
  raw.clear();
  raw.addInteraction(slot(1, 3));
  iact.publish();
  paintclock::notePainted();
  CHECK(iact.publishedRoutable());
  CHECK(tap().value == 3);

  // A second popup replaces it on the same object. Published, not yet painted:
  // a finger resting where the first popup's row was must not select the
  // second popup's row under it.
  iact.beginBuild();
  iact.beginPublishCycle();
  raw.clear();
  raw.addInteraction(slot(1, 9));
  iact.publish();
  CHECK(!iact.publishedRoutable());
  CHECK(!tap());

  paintclock::notePainted();
  CHECK(iact.publishedRoutable());
  CHECK(tap().value == 9);

  // The touch-down highlight repaint: same options, only StateFocused moves,
  // which the digest ignores. It must still answer, or every popup would
  // highlight a row and then do nothing.
  iact.beginBuild();
  iact.beginPublishCycle();
  raw.clear();
  freeink::ui::Interaction focused = slot(1, 9);
  focused.state = fui::StateFocused;
  raw.addInteraction(focused);
  iact.publish();
  CHECK(iact.publishedRoutable());
  CHECK(tap().value == 9);
}

// beginBuild() digests the PUBLISHED generation, not the one being built into.
// The two are the same array for a caller that never publishes, and for one
// that calls beginBuild() before beginPublishCycle() (which is what
// OptionPopup does). They diverge for a caller that flips generations FIRST,
// and then data() is the table from two generations ago -- compared against as
// though the panel had shown it. This drives that order deliberately, because
// nothing else in the suite can tell the two apart.
void testBeginBuildDigestsThePublishedGenerationNotTheBuildingOne() {
  PaintClockGuard clock;
  paintclock::RevealedInteractions<17> iact;
  freeink::ui::InteractionBuffer<17>& raw = iact;

  const auto put = [&raw](const int16_t value) {
    freeink::ui::Interaction hit{};
    hit.rect = fui::Rect{0, 0, 100, 40};
    hit.action = 1;
    hit.value = value;
    hit.inputMask = fui::InputTouch;
    raw.clear();
    raw.addInteraction(hit);
  };

  // Generation 1 ends up holding table A, generation 0 holding table B, and B
  // is what the panel is showing.
  iact.beginBuild();
  iact.beginPublishCycle();
  put(1);
  iact.publish();
  paintclock::notePainted();

  iact.beginBuild();
  iact.beginPublishCycle();
  put(2);
  iact.publish();
  paintclock::notePainted();
  CHECK(iact.publishedRoutable());

  // Now the order that matters: flip generations FIRST, so data() is the stale
  // A from two renders ago while publishedData() is still the B on the glass.
  iact.beginPublishCycle();
  iact.beginBuild();
  put(1);
  iact.publish();

  // The panel shows B and the table is A, so this tap must be held. Digesting
  // data() instead would have adopted the stale A as "shown", found the new
  // table identical to it, and let the tap straight through.
  CHECK(!iact.publishedRoutable());
  paintclock::notePainted();
  CHECK(iact.publishedRoutable());
}

// OptionPopup's real render sequence, through the SDK component it actually
// calls. The hand-built test above proves the gate; this proves the thing a
// hand-built table cannot -- that the touch-down HIGHLIGHT repaint produces a
// byte-identical table. Get that wrong and every popup in the firmware lights
// a row up and then refuses it, which is the frozen-device failure this whole
// mechanism is shaped around, and no assertion on the gate alone would notice.
void testAnOptionPopupHighlightRepaintStillAnswers() {
  PaintClockGuard clock;
  FakeTarget target;
  paintclock::RevealedInteractions<17> interactions;

  static const char* const kLabels[3] = {"ONE", "TWO", "THREE"};

  // Mirrors OptionPopup::render(): beginBuild() before the publish cycle, the
  // chrome guard rect first, the dialog after, publish() last.
  const auto build = [&](const int selectedIndex, const uint8_t count) {
    const fui::DeviceContext ctx = device();
    const fui::InputSnapshot noInput{};
    interactions.beginBuild();
    interactions.beginPublishCycle();
    fui::Frame<17> frame(target, ctx, noInput, interactions);

    fui::DialogOption options[3];
    for (uint8_t i = 0; i < count; ++i) {
      options[i].label = kLabels[i];
      options[i].action = 1;
      options[i].value = static_cast<int16_t>(i);
      options[i].state = (i == selectedIndex) ? fui::StateFocused : fui::StateNormal;
    }

    fui::OptionDialogProps props;
    props.title = "PICK";
    props.options = options;
    props.optionCount = count;
    props.verticalOptions = true;
    props.inputMask = fui::InputTouch;
    props.buttonHeight = 40;

    const fui::Rect dialog = fui::centeredRect(ctx.screen(), fui::Size{300, 300});
    frame.hit(dialog, 2, 0, fui::InputTouch);
    fui::optionDialog(frame, dialog, props);
    interactions.publish();
  };

  build(0, 3);
  paintclock::notePainted();
  CHECK(interactions.publishedRoutable());
  const size_t slots = interactions.publishedCount();
  CHECK(slots > 1);  // the guard plus at least one option, or this proves nothing

  // The highlight moving is the ONLY change. optionDialog derives each option
  // rect from geometry and the state only reaches Interaction::state, which the
  // digest reads for StateDisabled and nothing else -- so the release of the
  // contact that caused this repaint must still route.
  build(1, 3);
  CHECK(interactions.publishedRoutable());
  CHECK(interactions.publishedCount() == slots);
  build(2, 3);
  CHECK(interactions.publishedRoutable());

  // A different popup on the same object is a different table, and waits.
  build(0, 2);
  CHECK(!interactions.publishedRoutable());
  paintclock::notePainted();
  CHECK(interactions.publishedRoutable());
}

void testBattleshipStartMenu() {
  // A row that would do nothing is not drawn, exactly as in chess: with no
  // saved game there is nothing to continue, so the first row is NEW GAME.
  bshipui::StartModel fresh;
  fresh.played = 0;
  CHECK(bshipui::startRows(fresh) == 2);
  CHECK(bshipui::startRowAt(fresh, 0) == bshipui::StartRow::NewGame);
  CHECK(bshipui::startRowAt(fresh, 1) == bshipui::StartRow::PlayNearby);
  // Out of range clamps rather than reading past the end.
  CHECK(bshipui::startRowAt(fresh, 9) == bshipui::StartRow::PlayNearby);
  CHECK(bshipui::startRowAt(fresh, -1) == bshipui::StartRow::NewGame);

  bshipui::StartModel saved;
  saved.hasSavedGame = true;
  saved.played = 12;
  saved.won = 7;
  saved.streak = 3;
  CHECK(bshipui::startRows(saved) == 3);
  CHECK(bshipui::startRowAt(saved, 0) == bshipui::StartRow::Continue);

  Rendered out;
  buildBattleshipStart(out, saved);
  CHECK(out.target.drew("BATTLESHIP"));
  CHECK(out.target.drew("CONTINUE"));
  // No receipt beside the word: how the game stands is drawn in the slot this
  // builder returns, in the same marks the board uses.
  CHECK(!out.target.drew("14 SHOTS, 2 SUNK"));
  CHECK(out.target.drew("PLAY NEARBY"));
  // The record is one line, not three rows.
  CHECK(out.target.drew("12 PLAYED   7 WON   STREAK 3"));

  const FakeTarget::TextRun* nearby = out.target.find("PLAY NEARBY");
  CHECK(nearby != nullptr);
  if (nearby != nullptr) {
    const fui::ActionEvent event =
        out.tap(nearby->rect.x + nearby->rect.width / 2, nearby->rect.y + nearby->rect.height / 2);
    CHECK(event.action == bshipui::ActionStartRow);
    CHECK(bshipui::startRowAt(saved, event.value) == bshipui::StartRow::PlayNearby);
  }
}

void testBattleshipCapsuleIsOnlyATriggerWhenItSaysSo() {
  // The capsule does three jobs and the hit table has to agree with the label
  // every time. Chess shipped a PLAY AGAIN that was dead on its edges; these
  // assertions are that bug pinned for this app.
  Rendered reporting;
  bshipui::BoardModel model;
  model.status = "MARIO FIRED AT C4";
  buildBattleshipBoard(reporting, model);
  const FakeTarget::TextRun* label = reporting.target.find("MARIO FIRED AT C4");
  CHECK(label != nullptr);
  if (label != nullptr) {
    const fui::ActionEvent event =
        reporting.tap(label->rect.x + label->rect.width / 2, label->rect.y + label->rect.height / 2);
    CHECK(event.action == fui::NO_ACTION);
  }

  Rendered armed;
  bshipui::BoardModel aiming;
  aiming.status = "FIRE AT C4";
  aiming.canFire = true;
  buildBattleshipBoard(armed, aiming);
  const FakeTarget::TextRun* fire = armed.target.find("FIRE AT C4");
  CHECK(fire != nullptr);
  if (fire != nullptr) {
    CHECK(armed.tap(fire->rect.x + fire->rect.width / 2, fire->rect.y + fire->rect.height / 2).action ==
          bshipui::ActionFire);
    // Both edges, because a capsule painted wider than it hit-tests is exactly
    // how this went wrong before.
    CHECK(armed.tap(fire->rect.x + 2, fire->rect.y + fire->rect.height / 2).action == bshipui::ActionFire);
    CHECK(armed.tap(fire->rect.right() - 2, fire->rect.y + fire->rect.height / 2).action == bshipui::ActionFire);
  }

  Rendered finished;
  bshipui::BoardModel over;
  over.status = "PLAY AGAIN";
  over.gameOver = true;
  buildBattleshipBoard(finished, over);
  const FakeTarget::TextRun* again = finished.target.find("PLAY AGAIN");
  CHECK(again != nullptr);
  if (again != nullptr) {
    CHECK(finished.tap(again->rect.x + again->rect.width / 2, again->rect.y + again->rect.height / 2).action ==
          bshipui::ActionPlayAgain);
  }
}

// #243: the waiting capsule ("TAP A TARGET") must not draw the disabled-button
// dither. That style knocks white text out of a DarkGray dither, and on the
// panel a dither is a sparse pattern of black pixels: low-contrast to read and,
// being sparse, exactly what a partial refresh leaves residue from -- so the one
// control on the opening screen you most need to read was the one that ghosted.
// It is a status line, not a disabled control, so it keeps the solid capsule
// chess's inert status already uses, told apart from the armed FIRE by its label
// alone. The ghosting itself is analog and no host test can see it; the dither
// that causes it is what this pins, and it goes red on the borrowed style.
void testBattleshipWaitingCapsuleIsNotDithered() {
  Rendered out;
  bshipui::BoardModel waiting;  // not gameOver, not canFire: only reporting
  waiting.status = "TAP A TARGET";
  buildBattleshipBoard(out, waiting);

  const FakeTarget::TextRun* label = out.target.find("TAP A TARGET");
  CHECK(label != nullptr);
  if (label == nullptr) return;

  // The ground the label sits on, found by the label rather than by arithmetic
  // on the band. Later fills draw over earlier ones, so the last fill covering
  // the label's centre is the capsule's own ground.
  const int16_t cx = static_cast<int16_t>(label->rect.x + label->rect.width / 2);
  const int16_t cy = static_cast<int16_t>(label->rect.y + label->rect.height / 2);
  bool found = false;
  fui::Paint ground{};
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Rect r = out.target.fills[i];
    if (cx < r.x || cx >= r.right() || cy < r.y || cy >= r.bottom()) continue;
    ground = out.target.fillPaints[i];
    found = true;
  }
  CHECK(found);
  // Names the bug (the borrowed disabled dither) rather than the fix.
  CHECK(!(ground.kind == fui::PaintKind::Dither && ground.color == fui::Color::DarkGray));
  // And positively: the capsule draws solid, like FIRE and like chess's inert
  // status. Reinstate disabledButtonStyles() here and both checks go red.
  CHECK(ground.kind == fui::PaintKind::Solid);
}

void testBattleshipPlacementControls() {
  Rendered out;
  bshipui::PlaceModel model;
  model.status = "TAP A SHIP TO MOVE IT";
  buildBattleshipPlace(out, model);
  // "PLACE YOUR FLEET" came out of the band as "PLACE YOUR FLEE" on the device:
  // the display cut is wide and the header does not shrink to fit.
  CHECK(out.target.drew("YOUR FLEET"));
  CHECK(out.target.drew("TAP A SHIP TO MOVE IT"));
  CHECK(out.target.drew("SHUFFLE"));
  CHECK(out.target.drew("READY"));

  const FakeTarget::TextRun* shuffle = out.target.find("SHUFFLE");
  const FakeTarget::TextRun* ready = out.target.find("READY");
  CHECK(shuffle != nullptr && ready != nullptr);
  if (shuffle != nullptr && ready != nullptr) {
    // Two controls side by side, so the risk is one swallowing the other's
    // half of the footer. Each is checked at both its edges.
    CHECK(out.tap(shuffle->rect.x + 2, shuffle->rect.y + shuffle->rect.height / 2).action == bshipui::ActionShuffle);
    CHECK(out.tap(shuffle->rect.right() - 2, shuffle->rect.y + shuffle->rect.height / 2).action ==
          bshipui::ActionShuffle);
    CHECK(out.tap(ready->rect.x + 2, ready->rect.y + ready->rect.height / 2).action == bshipui::ActionReady);
    CHECK(out.tap(ready->rect.right() - 2, ready->rect.y + ready->rect.height / 2).action == bshipui::ActionReady);
    CHECK(shuffle->rect.right() < ready->rect.x);
  }

  // Waiting for the other device: the buttons stay where they are and stop
  // working, rather than vanishing and moving the grid.
  Rendered waiting;
  bshipui::PlaceModel sent;
  sent.status = "WAITING FOR MARIO";
  sent.canEdit = false;
  buildBattleshipPlace(waiting, sent);
  CHECK(waiting.target.drew("SHUFFLE"));
  CHECK(waiting.target.drew("READY"));
  const FakeTarget::TextRun* inert = waiting.target.find("READY");
  CHECK(inert != nullptr);
  if (inert != nullptr) {
    CHECK(waiting.tap(inert->rect.x + inert->rect.width / 2, inert->rect.y + inert->rect.height / 2).action ==
          fui::NO_ACTION);
  }
}

// --- a shelf folder --------------------------------------------------------

void buildShelf(Rendered& out, const shelfui::MenuModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  shelfui::buildMenu(screen, model);
}

void testShelfFolderDrawsItsOwnNameAndRows() {
  fui::ListItem items[4] = {};
  const char* titles[4] = {"BROWSE FILES", "BATTLESHIP", "SETTINGS", "SOLITAIRE"};
  for (int i = 0; i < 4; ++i) {
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  shelfui::MenuModel model;
  // One builder draws every folder, so the title is data, not a literal. If it
  // were hardcoded again the APPS folder would call itself GAMES.
  model.title = "APPS & GAMES";
  model.items = items;
  model.count = 4;
  model.playerName = "SPIKY GRIM BEARD";

  Rendered menu;
  buildShelf(menu, model);
  CHECK(menu.target.drew("APPS & GAMES"));
  CHECK(menu.target.drew("BROWSE FILES"));
  CHECK(menu.target.drew("SOLITAIRE"));
  CHECK(menu.target.drew("SPIKY GRIM BEARD"));
  CHECK(!menu.interactions.overflowed());

  const int firstRowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight / 2;
  const fui::ActionEvent first = menu.tap(240, firstRowY);
  CHECK(first.action == shelfui::ActionOpen);
  CHECK(first.value == 0);

  // The same builder, a different folder. Asserting the name changed is the
  // only thing standing between one builder and a hardcoded header.
  shelfui::MenuModel apps = model;
  apps.title = "SHELF";
  Rendered other;
  buildShelf(other, apps);
  CHECK(other.target.drew("SHELF"));
  CHECK(!other.target.drew("APPS & GAMES"));
}

// A folder with more rows than fit, which is every GAMES folder from the tenth
// game onward.
//
// The row icons are drawn by this fork rather than by the list component, so
// they carry their own idea of where a row is, and it used to be the absolute
// item index. That is the same thing as the row only while nothing scrolls. At
// ten items the tenth icon painted below the band in black, on top of the black
// player footer; once scrolled, every icon sat a row away from its label. The
// three shelf tests that already existed all used lists short enough to fit, so
// none of them could see it.
//
// Asserted as "each visible label has its own icon on its own row" rather than
// as a count, because the count was right the whole time the positions were
// wrong. A distinct icon per row is what makes an off-by-N detectable at all.
//
// Driven at both pages, because they fail differently and an earlier draft of
// this test only had the second. On page one the rows past the fold must simply
// not be drawn, which is the tenth-icon-on-the-footer case. On page two the
// drawn ones must have moved up with their labels.
void checkShelfIconsSitOnTheirRows(const int page) {
  constexpr int kCount = 12;
  const freeink::Icon* const palette[kCount] = {&icon_browse_32,    &icon_battleship_32, &icon_choose_32,
                                                &icon_solitaire_32, &icon_nearby_32,     &icon_settings_32,
                                                &icon_apps_32,      &icon_hackernews_32, &icon_unreadable_32,
                                                &icon_study_32,     &icon_xkcd_32,       &icon_wallpapers_32};

  fui::ListItem items[kCount] = {};
  char labels[kCount][8] = {};
  for (int i = 0; i < kCount; ++i) {
    std::snprintf(labels[i], sizeof(labels[i]), "GAME%02d", i);
    items[i].label = labels[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const fui::ThemeTokens tokens = toybox::themeTokens();
  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, true, kCount);
  // The list has to overflow one page or neither case under test exists.
  CHECK(paging.pageCount > 1);
  const fui::Rect band = shelfui::listBand(device(), true, true);

  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.page = page;
  model.pageCount = paging.pageCount;

  // The screen is handed one page, sliced, exactly as the activity hands it one.
  // The last page is short, so this is not always rowsPerPage.
  const int first = page * paging.rowsPerPage;
  const int onThisPage = kCount - first < paging.rowsPerPage ? kCount - first : paging.rowsPerPage;
  model.items = items + first;
  model.icons = palette + first;
  model.count = onThisPage;

  Rendered menu;
  buildShelf(menu, model);

  // Tight, because half a row was not. This read `tokens.rowHeight / 2` (31px)
  // on the reasoning that an icon one row out of place is a whole row away --
  // true of a clean off-by-one, and false of the drift that actually happened.
  // v1.13.4 moved each icon 4px further down than the last, so the eighth was a
  // full row out while the first was 3px out, and the average stayed under 31.
  // The suite was green on the screen in qa-artifacts/games-broken.png.
  //
  // An icon and its label are centred on the same row, so their midpoints agree
  // to within text metrics alone. Anything larger is a grid disagreement, which
  // is the whole class of bug this test exists for.
  const int tolerance = 8;
  int paired = 0;
  for (int i = 0; i < kCount; ++i) {
    const fui::Rect* icon = nullptr;
    for (const auto& blit : menu.target.blits) {
      if (blit.data == palette[i]->bits) {
        icon = &blit.rect;
        break;
      }
    }
    const fui::Rect* label = nullptr;
    for (const auto& run : menu.target.texts) {
      if (run.text == labels[i]) {
        label = &run.rect;
        break;
      }
    }

    // Scrolled off the top, or below the fold. The icon must be gone too: this
    // is the half that used to paint onto the player footer.
    if (label == nullptr) {
      CHECK(icon == nullptr);
      continue;
    }

    // Guarded rather than asserted-and-continued: a missing icon here used to
    // segfault the rest of the loop, which is a worse failure report than the
    // one line that is actually wrong.
    CHECK(icon != nullptr);
    if (icon == nullptr) continue;

    CHECK(icon->y >= band.y);
    CHECK(icon->y + icon->height <= band.y + band.height);
    const int iconMid = icon->y + icon->height / 2;
    const int labelMid = label->y + label->height / 2;
    CHECK(iconMid >= labelMid - tolerance && iconMid <= labelMid + tolerance);
    ++paired;
  }

  CHECK(paired == onThisPage);
}

// No row of a shelf folder is ever marked.
//
// The X4 Pro has two physical keys, both of which PAGE, and `frontButtonConfirm`
// resolves to an unassigned pin -- so an inverted row is a cursor that nothing
// can move and nothing can act on. It shipped as a landmark explaining why a
// restored folder did not open on page one, and it was read as a cursor
// instead: the row it marked was the last app opened, so APPS wore a permanent
// highlight on whichever app was used most.
//
// Asserted as ink rather than as a field so it survives the field: a selected
// row draws its label paper-on-black, so every label being ink is the property
// that actually matters, whatever the model grows later. The icons are checked
// the same way, because they are drawn by this fork rather than by the list
// component and used to invert on their own.
void testShelfFolderMarksNoRow() {
  constexpr int kCount = 5;
  const freeink::Icon* const palette[kCount] = {&icon_study_32, &icon_hackernews_32, &icon_xkcd_32, &icon_settings_32,
                                                &icon_apps_32};
  const char* titles[kCount] = {"STUDY", "HACKER NEWS", "XKCD", "GET BOOKS", "WALLPAPERS"};
  fui::ListItem items[kCount] = {};
  for (int i = 0; i < kCount; ++i) {
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  shelfui::MenuModel model;
  model.title = "SHELF";
  model.items = items;
  model.icons = palette;
  model.count = kCount;

  Rendered menu;
  buildShelf(menu, model);

  // Every row label present, and every one of them ink. White here would be a
  // row drawn inverted, which is the mark under test.
  int checked = 0;
  for (int i = 0; i < kCount; ++i) {
    const FakeTarget::TextRun* row = menu.target.find(titles[i]);
    CHECK(row != nullptr);
    if (row == nullptr) continue;
    CHECK(row->color == fui::Color::Black);
    ++checked;
  }
  CHECK(checked == kCount);

  // And the icons, which invert separately from the label.
  for (int i = 0; i < kCount; ++i) {
    for (const auto& blit : menu.target.blits) {
      if (blit.data == palette[i]->bits) CHECK(blit.color == fui::Color::Black);
    }
  }
}

// --- the chooser ------------------------------------------------------------
//
// The corner chip, the boxes it puts on the rows, and the empty folder that a
// person who hides everything lands in. What is being defended here is not that
// the mode works: it is that entering it does not move the list. Every page of
// every folder draws its rows at the same eight screen positions, so a list
// that shifted under a mode switch is indistinguishable from one that did not
// until something opens -- which on this screen has already cost one cold
// tester the wrong game (docs/shelf.md).

// The same artwork, by its BYTES rather than by its address. ToyboxIcons.h
// declares every icon `static const`, so the copy the screen builder blits is a
// different object from the copy this test can name -- one per translation
// unit. An icon the test hands IN through the model compares by pointer; one
// the builder reaches for itself, like this tick, cannot.
bool sameIcon(const uint8_t* drawn, const freeink::Icon& icon) {
  if (drawn == nullptr) return false;
  const size_t bytes = static_cast<size_t>((icon.w + 7) / 8) * icon.h;
  return std::memcmp(drawn, icon.bits, bytes) == 0;
}

// One folder's worth of rows, for the tests below: enough to page, with the
// player bar GAMES carries.
struct ChooserFixture {
  static constexpr int kCount = 12;
  char labels[kCount][8] = {};
  fui::ListItem items[kCount] = {};
  bool checks[kCount] = {};
  const freeink::Icon* icons[kCount] = {};

  ChooserFixture() {
    for (int i = 0; i < kCount; ++i) {
      std::snprintf(labels[i], sizeof(labels[i]), "GAME%02d", i);
      items[i].label = labels[i];
      items[i].actionValue = static_cast<int16_t>(i);
      checks[i] = true;
      icons[i] = &icon_browse_32;
    }
  }

  shelfui::MenuModel page(const int first, const int onThisPage, const bool choosing) {
    shelfui::MenuModel model;
    model.title = "APPS & GAMES";
    model.items = items + first;
    model.icons = icons + first;
    model.count = onThisPage;
    model.checks = choosing ? checks + first : nullptr;
    // Set in BOTH modes: the name is a fact about the folder, and the screen
    // decides what goes in the band it buys -- the player bar while browsing,
    // the chooser's caption while choosing. A model that dropped the name while
    // choosing would drop the band with it and reflow the list.
    model.playerName = "SPIKY GRIM BEARD";
    return model;
  }
};

void testTheHeaderBandOpensAndClosesTheChooser() {
  ChooserFixture fixture;

  // Browsing: no button anywhere. The band is the way in and the folder's mark
  // is what sits in it, which is the whole of Mario's redirection -- a
  // permanent EDIT chip was the first design and it shouted on every visit for
  // a thing done once.
  Rendered browsing;
  shelfui::MenuModel model = fixture.page(0, 6, false);
  model.mark = &icon_settings_32;
  buildShelf(browsing, model);
  CHECK(!browsing.target.drew(shelfui::kDoneChip));
  CHECK(!browsing.target.drew("EDIT"));

  bool drewTheMark = false;
  for (const auto& blit : browsing.target.blits) {
    if (blit.data != icon_settings_32.bits) continue;
    drewTheMark = true;
    // On the band, in the corner, and in PAPER: the band is solid black and a
    // mark drawn in ink there is not there at all.
    CHECK(blit.rect.y < toybox::kHeaderHeight);
    CHECK(blit.rect.right() > 480 - 60);
    CHECK(blit.color == fui::Color::White);
  }
  CHECK(drewTheMark);

  // The band answers a tap on the mark, on the title, and in the empty middle:
  // a 32px glyph is under half a thumb, so the target is the strip.
  CHECK(browsing.tap(456, 40).action == shelfui::ActionChoose);
  CHECK(browsing.tap(60, 40).action == shelfui::ActionChoose);
  CHECK(browsing.tap(240, 40).action == shelfui::ActionChoose);

  // Choosing: the corner becomes the way OUT, because a mode whose exit is
  // invisible is a trap. Same action, so the band still closes it too.
  Rendered choosing;
  shelfui::MenuModel chooser = fixture.page(0, 6, true);
  chooser.mark = &icon_settings_32;
  buildShelf(choosing, chooser);
  CHECK(choosing.target.drew(shelfui::kDoneChip));
  bool markWhileChoosing = false;
  for (const auto& blit : choosing.target.blits) {
    if (blit.data == icon_settings_32.bits) markWhileChoosing = true;
  }
  CHECK(!markWhileChoosing);

  const FakeTarget::TextRun* done = choosing.target.find(shelfui::kDoneChip);
  CHECK(done != nullptr);
  if (done != nullptr) {
    CHECK(choosing.tap(done->rect.x + done->rect.width / 2, done->rect.y + done->rect.height / 2).action ==
          shelfui::ActionChoose);
  }
  CHECK(choosing.tap(60, 40).action == shelfui::ActionChoose);
}

// The page counter shares the right-hand end of the band with whatever is in the
// corner -- the folder's mark while browsing, DONE while choosing, and they are
// not the same width. It used to be placed by hand at a hardcoded offset, which
// is fine for exactly one of those two and wrong for the other.
void testThePageCounterClearsTheCorner() {
  ChooserFixture fixture;
  for (const bool choosing : {false, true}) {
    Rendered menu;
    shelfui::MenuModel model = fixture.page(0, 6, choosing);
    model.mark = &icon_settings_32;
    model.page = 1;
    model.pageCount = 3;
    buildShelf(menu, model);

    const FakeTarget::TextRun* counter = menu.target.find("2/3");
    CHECK(counter != nullptr);
    if (counter == nullptr) continue;
    // Paper: the band is solid black, and a label left at the token's default
    // colour is painted black on black and simply is not there.
    CHECK(counter->color == fui::Color::White);
    if (choosing) {
      const FakeTarget::TextRun* chip = menu.target.find(shelfui::kDoneChip);
      CHECK(chip != nullptr);
      if (chip != nullptr) CHECK(counter->rect.right() <= chip->rect.x);
      continue;
    }
    // Browsing, the corner holds the folder's mark instead, and the counter has
    // to clear THAT -- which is what header.rightReserve buys.
    for (const auto& blit : menu.target.blits) {
      if (blit.data != icon_settings_32.bits) continue;
      CHECK(counter->rect.right() <= blit.rect.x);
      // And sit on the same line as it. Both are centred on their own INK in
      // the visible band, which is the rule that makes them agree; the header
      // component's rightLabel slot bottom-aligns to the TITLE's line box
      // instead, and a display cut's line box runs well below its glyphs, so
      // the counter landed under the baseline and read as dropped.
      const int16_t counterInkCentre =
          static_cast<int16_t>(counter->rect.y + toybox::kUiCut.ascender - toybox::kUiCut.inkHeight / 2);
      const int16_t markCentre = static_cast<int16_t>(blit.rect.y + blit.rect.height / 2);
      CHECK(std::abs(counterInkCentre - markCentre) <= 2);
    }
  }
}

// Entering the chooser must not reflow the list. This is the property the whole
// mode is arranged around, and it is asserted where it can actually fail: the
// same folder rendered both ways, with every label required to land on the same
// pixel row.
//
// The first version of this test compared pagingFor() against itself -- both
// arguments reduced to the same bool -- and would have passed against an
// implementation that reflowed. What follows goes through the builder.
void checkTheChooserKeepsTheRowsWhereTheyWere(const bool showsDeviceName) {
  ChooserFixture fixture;
  const int first = 0;
  const int onThisPage = 6;

  Rendered browsing;
  shelfui::MenuModel a = fixture.page(first, onThisPage, false);
  a.playerName = showsDeviceName ? "SPIKY GRIM BEARD" : nullptr;
  buildShelf(browsing, a);

  Rendered choosing;
  shelfui::MenuModel b = fixture.page(first, onThisPage, true);
  b.playerName = showsDeviceName ? "SPIKY GRIM BEARD" : nullptr;
  buildShelf(choosing, b);

  int compared = 0;
  for (int i = 0; i < onThisPage; ++i) {
    const FakeTarget::TextRun* before = browsing.target.find(fixture.labels[first + i]);
    const FakeTarget::TextRun* after = choosing.target.find(fixture.labels[first + i]);
    CHECK(before != nullptr);
    CHECK(after != nullptr);
    if (before == nullptr || after == nullptr) continue;
    // The label moves RIGHT by the box's gutter, and must not move DOWN at all.
    CHECK(before->rect.y == after->rect.y);
    CHECK(after->rect.x > before->rect.x);
    ++compared;
  }
  CHECK(compared == onThisPage);

  // And a FULL page, both ways, because that is where a band the mode took for
  // itself would actually show: the activity hands the builder as many rows as
  // pagingFor promised, and a builder that then reserved a strip of its own
  // would drop the last one -- no crash, no log, just a game that is not on the
  // page the counter says it is on.
  const fui::ThemeTokens tokens = toybox::themeTokens();
  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, showsDeviceName, 40);
  CHECK(paging.rowsPerPage > 0);
  CHECK(paging.pageCount > 1);

  std::vector<std::string> labels(static_cast<size_t>(paging.rowsPerPage));
  std::vector<fui::ListItem> full(static_cast<size_t>(paging.rowsPerPage));
  std::vector<bool> shown(static_cast<size_t>(paging.rowsPerPage), true);
  std::vector<char> flags(static_cast<size_t>(paging.rowsPerPage), 1);
  for (int i = 0; i < paging.rowsPerPage; ++i) {
    labels[static_cast<size_t>(i)] = "FULL" + std::to_string(i);
    full[static_cast<size_t>(i)].label = labels[static_cast<size_t>(i)].c_str();
    full[static_cast<size_t>(i)].actionValue = static_cast<int16_t>(i);
  }

  for (const bool choosingNow : {false, true}) {
    Rendered page;
    shelfui::MenuModel model;
    model.title = "APPS & GAMES";
    model.items = full.data();
    model.count = paging.rowsPerPage;
    model.checks = choosingNow ? reinterpret_cast<const bool*>(flags.data()) : nullptr;
    model.playerName = showsDeviceName ? "SPIKY GRIM BEARD" : nullptr;
    model.page = 0;
    model.pageCount = paging.pageCount;
    buildShelf(page, model);
    int drawn = 0;
    for (int i = 0; i < paging.rowsPerPage; ++i) {
      if (page.target.drew(labels[static_cast<size_t>(i)].c_str())) ++drawn;
    }
    CHECK(drawn == paging.rowsPerPage);
    CHECK(!page.interactions.overflowed());
  }
}

void testTheChooserKeepsTheSamePageGeometry() {
  // GAMES, which has the player bar the caption borrows.
  checkTheChooserKeepsTheRowsWhereTheyWere(true);
  // And APPS, which has no bar at all -- the case a mode-owned band would have
  // reflowed, ten rows browsing against nine choosing.
  checkTheChooserKeepsTheRowsWhereTheyWere(false);
}

// A box on every row, filled for a game on the list and outlined for one that
// is off it, and the tick only on the filled ones. Asserted as a count of each
// rather than "a box was drawn", because the two states are the whole control:
// a chooser that drew the same box on every row would pass any test that only
// looked for boxes.
void testTheChooserDrawsABoxPerRowAndTicksTheShownOnes() {
  ChooserFixture fixture;
  fixture.checks[1] = false;
  fixture.checks[3] = false;

  Rendered menu;
  shelfui::MenuModel model = fixture.page(0, 6, true);
  buildShelf(menu, model);

  int ticks = 0;
  for (const auto& blit : menu.target.blits) {
    if (!sameIcon(blit.data, icon_tick_24)) continue;
    ++ticks;
    // Paper on the slab. Ink would be invisible and nothing would warn.
    CHECK(blit.color == fui::Color::White);
  }
  CHECK(ticks == 4);

  // The four filled slabs are the ticks' own grounds, and the two hidden rows
  // are outlines instead: an outline is a stroke, and nothing else on this
  // screen strokes a 32px square.
  int outlines = 0;
  for (const auto& stroke : menu.target.strokes) {
    if (stroke.rect.width == toybox::kIconSize && stroke.rect.height == toybox::kIconSize) ++outlines;
  }
  CHECK(outlines == 2);

  // The app's own icon is still on the right of every row: the box is a second
  // mark, not a replacement for the first.
  int appIcons = 0;
  for (const auto& blit : menu.target.blits) {
    if (blit.data == icon_browse_32.bits) ++appIcons;
  }
  CHECK(appIcons == 6);

  // And the caption, which is the only thing on the panel that says a tap now
  // changes a row rather than opening one. Measured rather than merely found:
  // the first wording was four characters too wide for the band, the renderer
  // ellipsized it to "TAP A ROW TO SHOW OR HI..." on the panel, and drew() saw
  // the string the builder handed over and passed.
  CHECK(drewLabelWhole(menu, "TAP TO SHOW OR HIDE"));
  CHECK(!menu.target.drew("SPIKY GRIM BEARD"));
}

// The caption and the empty folder's sentences have a PIXEL budget, and the
// fake target's ten-pixel cell is half the panel's.
//
// This is the trap that got the first wording: "TAP A ROW TO SHOW OR HIDE IT"
// measured 280px here and fit the 448px band, and came back from the simulator
// as "TAP A ROW TO SHOW OR HI...". The renderer ellipsizes and logs nothing, so
// only a measurement can see it -- and only one taken against a cell the size
// of the real cut. Twenty is conservative for toybox_20, whose capitals run
// about nineteen.
void testTheChooserWordsFitTheirBands() {
  ChooserFixture fixture;
  Rendered menu;
  menu.target.charW = 20;
  shelfui::MenuModel model = fixture.page(0, 6, true);
  buildShelf(menu, model);
  CHECK(drewLabelWhole(menu, "TAP TO SHOW OR HIDE"));

  // And the empty folder, whose headline is set in the DISPLAY cut -- the
  // widest in the fork, and the one with the least room to be wrong in.
  Rendered empty;
  empty.target.charW = 30;
  shelfui::MenuModel nothing;
  nothing.title = "APPS & GAMES";
  nothing.count = 0;
  nothing.playerName = "SPIKY GRIM BEARD";
  buildShelf(empty, nothing);
  CHECK(drewLabelWhole(empty, "NOTHING HERE"));
  // The sentence under it wraps rather than truncating, so what it must not do
  // is need more lines than the rect reserved for it.
  const FakeTarget::TextRun* hint = empty.target.find("TAP TO CHOOSE WHAT THIS FOLDER SHOWS");
  CHECK(hint != nullptr);
  if (hint != nullptr) CHECK(uncappedWrappedHeight(empty.target, *hint) <= hint->rect.height);
}

// A row in the chooser toggles. It must not open: the same pixel means "play
// CHESS" one tap earlier, and a mode read from anywhere but the model is how
// that goes wrong.
void testAChooserRowTogglesInsteadOfOpening() {
  ChooserFixture fixture;
  const int firstRowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight / 2;

  Rendered browsing;
  shelfui::MenuModel model = fixture.page(0, 6, false);
  buildShelf(browsing, model);
  const fui::ActionEvent opens = browsing.tap(240, firstRowY);
  CHECK(opens.action == shelfui::ActionOpen);
  CHECK(opens.value == 0);

  Rendered choosing;
  shelfui::MenuModel chooser = fixture.page(0, 6, true);
  buildShelf(choosing, chooser);
  const fui::ActionEvent toggles = choosing.tap(240, firstRowY);
  CHECK(toggles.action == shelfui::ActionToggleShown);
  CHECK(toggles.value == 0);

  // The value is the row's place in the whole list, not in the page, so the
  // second page reports the games it is showing rather than rows 0-5 again.
  Rendered second;
  shelfui::MenuModel later = fixture.page(6, 6, true);
  buildShelf(second, later);
  const fui::ActionEvent sixth = second.tap(240, firstRowY);
  CHECK(sixth.action == shelfui::ActionToggleShown);
  CHECK(sixth.value == 6);
}

// Hiding everything is allowed, and the folder it leaves must not be a dead
// end. The whole empty band is the way back in -- the chip is 400px away at the
// top of an 800px panel, and a caption pointing at a control the reader has not
// found is worse than no caption at all.
void testAnEmptyFolderIsItsOwnWayBack() {
  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.count = 0;
  model.playerName = "SPIKY GRIM BEARD";

  Rendered menu;
  buildShelf(menu, model);
  CHECK(menu.target.drew("NOTHING HERE"));

  const FakeTarget::TextRun* headline = menu.target.find("NOTHING HERE");
  CHECK(headline != nullptr);
  if (headline != nullptr) {
    // Off the band, so it has to be ink. The display cut's token colour is
    // paper, and taken as given here the sentence is white on white.
    CHECK(headline->color == fui::Color::Black);
    // The sentence under it, and the tap that acts on it. Both are the same
    // band, so the tap is checked well away from the words.
    CHECK(menu.tap(240, headline->rect.y + 200).action == shelfui::ActionChoose);
    CHECK(menu.tap(240, headline->rect.y).action == shelfui::ActionChoose);
  }

  // And nothing claims to be a row.
  CHECK(!menu.interactions.overflowed());
}

// The token the fork positions rows BY is the geometry the list draws WITH.
//
// These are two different numbers in the SDK and nothing makes them agree.
// Screen::resolveListProps() sizes a row from its label font, its padding and
// the device touch minimum; theme().rowHeight is not an input to it. But
// toybox::listRowRect -- and so every icon drawn by iconAtRowRight, on nine
// screens -- computes its row grid from theme().rowHeight and listRowGap.
// While the two agree the icons sit on their rows. When they stopped agreeing
// (v1.13.4: 62/4 intended, 56/6 resolved) every icon walked 4px further down
// per row until the last one fell outside the band, and the shelf reserved
// rows at the wrong pitch and left dead space under the list.
//
// Asserted on BOTH device shapes because the divergence was touch-only: the
// clamps that overrode the theme are listTouchMinRowHeight and
// listTouchRowGap, so a non-touch check alone would have stayed green through
// the whole regression.
void testToyboxRowGeometryIsWhatTheListActuallyUses() {
  const fui::ThemeTokens tokens = toybox::themeTokens();

  for (const bool touch : {true, false}) {
    fui::DeviceContext ctx = device();
    ctx.hasTouch = touch;

    Rendered out;
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, tokens);

    // A list with nothing set: exactly what every Toybox screen passes, and
    // the case resolveListProps computes rather than takes.
    const fui::ListProps resolved = screen.resolveListProps(fui::ListProps{});

    CHECK(resolved.rowHeight == tokens.rowHeight);
    CHECK(resolved.rowGap == tokens.listRowGap);
  }
}

void testShelfIconsFollowTheRowsWhenTheListScrolls() {
  // Page one of a folder that overflows: the rows past the fold are the ones
  // that used to paint their icons onto the player footer.
  checkShelfIconsSitOnTheirRows(0);
  // And page two, where every drawn icon has moved up by a page and the ones
  // above the band must be gone.
  checkShelfIconsSitOnTheirRows(1);
}

// The shelf pages rather than scrolls, which is what makes a folder of forty
// games reachable on a panel whose only gesture is a tap: there is no swipe
// anywhere in this fork, and the list component's 3px overflow track is drawn
// but not tappable, so before this every row past the ninth could be reached
// only with the physical buttons.
void testTheShelfPagesWhenAFolderOverflows() {
  constexpr int kCount = 12;
  fui::ListItem items[kCount] = {};
  char labels[kCount][8] = {};
  for (int i = 0; i < kCount; ++i) {
    std::snprintf(labels[i], sizeof(labels[i]), "GAME%02d", i);
    items[i].label = labels[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const fui::ThemeTokens tokens = toybox::themeTokens();

  // A folder that fits pays nothing for paging: no bar, and every row it could
  // hold before it is still there.
  const shelfui::Paging small = shelfui::pagingFor(device(), tokens, true, 3);
  CHECK(small.pageCount == 1);
  CHECK(small.rowsPerPage ==
        fui::listVisibleRows(shelfui::listBand(device(), true, false), tokens.rowHeight, tokens.listRowGap));

  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, true, kCount);
  CHECK(paging.pageCount == 2);
  // The bar costs a row, so a paged folder holds fewer than an unpaged one.
  CHECK(paging.rowsPerPage < small.rowsPerPage);
  CHECK(paging.rowsPerPage * paging.pageCount >= kCount);

  // Every item is on exactly one page. This is the assertion that catches the
  // list component clamping topIndex to count - visible so its last screen is
  // full (list.h:164): under that rule page two of twelve showed items four to
  // eleven, repeating half of page one. It is why the screen is handed a slice.
  for (int page = 0; page < paging.pageCount; ++page) {
    const int first = page * paging.rowsPerPage;
    const int onThisPage = kCount - first < paging.rowsPerPage ? kCount - first : paging.rowsPerPage;

    shelfui::MenuModel model;
    model.title = "APPS & GAMES";
    model.playerName = "SPIKY GRIM BEARD";
    model.items = items + first;
    model.count = onThisPage;
    model.page = page;
    model.pageCount = paging.pageCount;

    Rendered menu;
    buildShelf(menu, model);
    for (int i = 0; i < kCount; ++i) {
      const bool belongsHere = i >= first && i < first + onThisPage;
      CHECK(menu.target.drew(labels[i]) == belongsHere);
    }
  }

  // And the pips are reachable. Rendered page one, tapping the bar must offer
  // every other page, because being able to leave page one is the entire point.
  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.items = items;
  model.count = paging.rowsPerPage;
  model.page = 0;
  model.pageCount = paging.pageCount;

  Rendered menu;
  buildShelf(menu, model);
  const fui::Rect band = shelfui::listBand(device(), true, true);

  // Found by probing rather than by recomputing the layout, so the test cannot
  // agree with the builder by making the same arithmetic mistake twice.
  int barY = -1;
  for (int y = band.y + band.height; y < 800 && barY < 0; ++y) {
    if (menu.tap(device().width / 2, y).action == shelfui::ActionGoToPage) barY = y;
  }
  CHECK(barY > 0);
  CHECK(barY > band.y + band.height);

  // Every page is one tap away, and the targets are contiguous *within the
  // cluster*: a sweep hits pages in ascending order with no dead pixel between
  // the first target and the last. Outside the cluster there is deliberately
  // nothing, because the marks are a position indicator with air around them
  // rather than a bar of buttons -- so this asserts no gap rather than no miss.
  // A gap between adjacent pages is a strip the thumb finds and the eye cannot.
  int reached[8] = {};
  int firstHit = -1;
  int lastHit = -1;
  int gaps = 0;
  int previous = -1;
  for (int x = toybox::kMargin; x < device().width - toybox::kMargin; ++x) {
    const fui::ActionEvent hit = menu.tap(x, barY);
    if (hit.action != shelfui::ActionGoToPage) {
      if (firstHit >= 0 && lastHit == x - 1) continue;  // past the cluster's end
      continue;
    }
    CHECK(hit.value >= 0 && hit.value < paging.pageCount);
    if (firstHit < 0) firstHit = x;
    if (lastHit >= 0 && x != lastHit + 1) ++gaps;
    // Ascending left to right: page one is on the left, as it reads.
    CHECK(hit.value >= previous);
    previous = hit.value;
    lastHit = x;
    if (hit.value < 8) ++reached[hit.value];
  }
  CHECK(firstHit > 0);
  CHECK(gaps == 0);
  for (int p = 0; p < paging.pageCount; ++p) CHECK(reached[p] > 0);
  // A cluster, not the whole bar: it must leave the edges alone or it is the
  // control this was rewritten to stop being.
  CHECK(lastHit - firstHit < band.width - 2 * toybox::kMargin);
}

// One input, one page, and the same page whichever input it was.
//
// The shelf pages from three places -- the two side keys, a horizontal swipe and
// a tap on a page mark -- and they used to do their own modular arithmetic each.
// Asserted as arithmetic because arithmetic is the half a cold tester cannot
// see: three of them reported a single press advancing two pages, and the press
// was never the variable. Where the folder had OPENED was.
void testAPageStepMovesExactlyOnePage() {
  CHECK(shelfui::pageStep(0, 3, 1) == 1);
  CHECK(shelfui::pageStep(1, 3, 1) == 2);
  // Wraps, because there is no cursor to run off the end of.
  CHECK(shelfui::pageStep(2, 3, 1) == 0);
  CHECK(shelfui::pageStep(0, 3, -1) == 2);
  CHECK(shelfui::pageStep(2, 3, -1) == 1);
  CHECK(shelfui::pageStep(1, 3, -1) == 0);
  // A folder that fits has nowhere to step to, and a key that quietly moved the
  // resumed row to the top instead would be a step that changed something
  // without going anywhere.
  CHECK(shelfui::pageStep(0, 1, 1) == 0);
  CHECK(shelfui::pageStep(0, 1, -1) == 0);

  // The property, not three examples of it: from any page of any folder, a step
  // moves by exactly one page and the opposite step undoes it. A guard that
  // fixed a double advance by making the key dead passes every example above
  // and fails the second line here.
  for (int pages = 2; pages <= 6; ++pages) {
    for (int from = 0; from < pages; ++from) {
      const int forward = shelfui::pageStep(from, pages, 1);
      const int back = shelfui::pageStep(from, pages, -1);
      CHECK((forward - from + pages) % pages == 1);
      CHECK((from - back + pages) % pages == 1);
      CHECK(shelfui::pageStep(forward, pages, -1) == from);
      CHECK(shelfui::pageStep(back, pages, 1) == from);
    }
  }
}

// The shelf's own step STOPS at both ends, and that is the fix for a wrong game
// being launched twice by two different testers.
//
// Every page of a folder draws its rows at the same eight screen positions, so
// a page arrived at by accident is indistinguishable from the page that was
// wanted until something opens. Walking forward off the last page is the step
// nobody ever means; with a wrap it silently rehomes you two pages back, and the
// next tap opens the game that happens to sit in that row instead.
void testTheShelfStepStopsAtBothEnds() {
  CHECK(shelfui::pageStepClamped(0, 3, 1) == 1);
  CHECK(shelfui::pageStepClamped(1, 3, 1) == 2);
  CHECK(shelfui::pageStepClamped(1, 3, -1) == 0);
  // The two that a wrap gets wrong, and the whole reason this exists.
  CHECK(shelfui::pageStepClamped(2, 3, 1) == 2);
  CHECK(shelfui::pageStepClamped(0, 3, -1) == 0);
  // A folder that fits has nowhere to step to at all.
  CHECK(shelfui::pageStepClamped(0, 1, 1) == 0);
  CHECK(shelfui::pageStepClamped(0, 1, -1) == 0);

  // The property, not five examples of it: a step lands on a real page, moves by
  // at most one, and moves by exactly one unless it was already at that end.
  // Written as a property because the failure it guards is arithmetic that only
  // misbehaves at the two rows nobody writes an example for.
  for (int pages = 2; pages <= 6; ++pages) {
    for (int from = 0; from < pages; ++from) {
      const int forward = shelfui::pageStepClamped(from, pages, 1);
      const int back = shelfui::pageStepClamped(from, pages, -1);
      CHECK(forward >= 0 && forward < pages);
      CHECK(back >= 0 && back < pages);
      CHECK(forward == (from == pages - 1 ? from : from + 1));
      CHECK(back == (from == 0 ? from : from - 1));
      // Never around the horn. A wrap satisfies every line above except these.
      CHECK(forward >= from);
      CHECK(back <= from);
    }
  }

  // A stored row that outlived its folder still lands on a page that exists, so
  // a step from it cannot walk off either end.
  CHECK(shelfui::pageStepClamped(9, 3, 1) == 2);
  CHECK(shelfui::pageStepClamped(-4, 3, -1) == 0);
}

// A folder comes back to the page it was left on, and it is a ROW that carries
// that across the reboot.
//
// Mario, on the device, after the restored page had been made visible: "if I
// navigate to page two and then leave to read a book and then come back, I
// should still be taken to page two." What was stored was the page holding the
// game he last LAUNCHED, which is the same page right up until he browses and
// walks away, and browsing and walking away is most of what a shelf is for.
//
// Asserted as arithmetic because the activity that writes the row cannot be
// built here -- it needs the ActivityManager. What can be pinned down here is
// the pair of rules that make the stored row mean a page at all: that a page
// round-trips through the row that stands for it, and what happens when the page
// it stood for is gone.
void testAFolderComesBackToThePageItWasLeftOn() {
  // A page is stored as its first row, and comes back as the same page. Every
  // page of every plausible folder, not three examples: a stored row that
  // reopened one page out is the original bug wearing different clothes.
  for (int rows = 1; rows <= 12; ++rows) {
    for (int page = 0; page < 9; ++page) {
      CHECK(shelfui::pageFor(shelfui::rowForPage(page, rows), rows) == page);
    }
  }
  // The first row of page one is the top of the list, which is where a folder
  // nobody has left anywhere opens: an unvisited folder needs no stored value to
  // behave, and page zero must not be a special case anywhere else either.
  CHECK(shelfui::rowForPage(0, 9) == 0);

  // A row inside the folder is where it says it is.
  CHECK(shelfui::resumeRowFor(0, 19) == 0);
  CHECK(shelfui::resumeRowFor(13, 19) == 13);
  CHECK(shelfui::resumeRowFor(18, 19) == 18);

  // A row past the end lands on the LAST page, never back at the top. This is
  // the removed-game case: the card outlives the firmware that wrote it, so the
  // folder can be shorter than it was, and page one throws away the one thing
  // that was remembered.
  for (int count = 1; count <= 24; ++count) {
    for (int rows = 1; rows <= 10; ++rows) {
      const int last = shelfui::pageCountFor(count, rows) - 1;
      for (int stored = count; stored < count + 30; ++stored) {
        const int row = shelfui::resumeRowFor(stored, count);
        CHECK(row == count - 1);
        CHECK(shelfui::pageFor(row, rows) == last);
      }
    }
  }

  // And the shrink is a real one, not a folder that collapsed to a single page:
  // nineteen games remembered at the end, two removed, still the last page and
  // still not page one. A "fix" that reset an out-of-range row to the top passes
  // every check above this one and fails these two.
  constexpr int kWas = 19;
  constexpr int kNow = 17;
  const shelfui::Paging paging = shelfui::pagingFor(device(), toybox::themeTokens(), true, kNow);
  CHECK(paging.pageCount > 1);
  const int resumed = shelfui::pageFor(shelfui::resumeRowFor(kWas - 1, kNow), paging.rowsPerPage);
  CHECK(resumed == paging.pageCount - 1);
  CHECK(resumed != 0);

  // An empty folder has one page and it is page one. There is no such folder in
  // the registry today, and the arithmetic must not divide by it if there ever
  // is: a folder that shrank to nothing is the limit of the case above.
  CHECK(shelfui::resumeRowFor(7, 0) == 0);
  CHECK(shelfui::pageFor(shelfui::resumeRowFor(7, 0), 9) == 0);
  CHECK(shelfui::pageCountFor(0, 9) == 1);

  // A corrupt or negative row is the top, which is also what an unwritten file
  // gives. Nothing here may go below zero and index off the front of a page.
  CHECK(shelfui::resumeRowFor(-4, 19) == 0);
  CHECK(shelfui::rowForPage(-1, 9) == 0);
  CHECK(shelfui::resumeRowFor(5, -1) == 0);
}

// The marks are a control, and a control has to look like one.
//
// They were always tappable and always the reliable way to page; two cold
// testers found them by accident and a third never tried them, because ten
// pixels of ink with air around them read as decoration. The frame is the
// smallest thing here that reads as touchable, and it has to sit on exactly the
// strip the taps land in or it promises a hit where there is none.
void testThePageMarksReadAsAControl() {
  constexpr int kCount = 20;
  fui::ListItem items[kCount] = {};
  for (int i = 0; i < kCount; ++i) {
    items[i].label = "GAME";
    items[i].actionValue = static_cast<int16_t>(i);
  }

  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const shelfui::Paging paging = shelfui::pagingFor(device(), tokens, true, kCount);
  CHECK(paging.pageCount > 1);

  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.items = items;
  model.count = paging.rowsPerPage;
  model.page = 0;
  model.pageCount = paging.pageCount;

  Rendered menu;
  buildShelf(menu, model);
  const fui::Rect band = shelfui::listBand(device(), true, true);

  // Probed, not recomputed, so the test cannot make the builder's arithmetic
  // mistake twice. Both edges of the strip, because the ink has to sit ON the
  // strip the taps land in: ink outside it promises a hit where there is none,
  // and that is the half a screenshot cannot show.
  int barY = -1;
  int barBottom = -1;
  for (int y = band.y + band.height; y < 800; ++y) {
    if (menu.tap(device().width / 2, y).action != shelfui::ActionGoToPage) continue;
    if (barY < 0) barY = y;
    barBottom = y;
  }
  CHECK(barY > 0);
  CHECK(barBottom > barY);

  int firstHit = -1;
  int lastHit = -1;
  for (int x = 0; x < device().width; ++x) {
    if (menu.tap(x, barY).action != shelfui::ActionGoToPage) continue;
    if (firstHit < 0) firstHit = x;
    lastHit = x;
  }
  CHECK(firstHit > 0);

  // It stays a cluster: ink as wide as the list is the bar of slabs the marks
  // were deliberately rewritten not to be.
  CHECK(lastHit - firstHit < band.width);

  const int pitch = (lastHit - firstHit + 1) / model.pageCount;
  CHECK(pitch > 20);

  // Every page carries a box of ink filling most of its own cell, and the
  // current one is FILLED where the others are outlined. Ten pixels of ink in a
  // forty-four pixel cell -- what this replaced, and what a cold tester called
  // "the size of a full stop" -- passes "something was drawn down there" and
  // fails the width check here.
  for (int p = 0; p < model.pageCount; ++p) {
    const int left = firstHit + p * pitch;
    const int right = left + pitch - 1;
    const auto ownCell = [&](const fui::Rect& r) {
      if (r.y < barY || r.y + r.height - 1 > barBottom) return false;
      if (r.x < left || r.x + r.width - 1 > right) return false;
      return r.width * 2 >= pitch;
    };
    int filled = 0;
    int outlined = 0;
    for (const auto& r : menu.target.fills) {
      if (ownCell(r)) ++filled;
    }
    for (const auto& s : menu.target.strokes) {
      if (s.width > 0 && ownCell(s.rect)) ++outlined;
    }
    // Asserted as a pair, both ways round: a mutant that filled every cell says
    // you are on all three pages, and one that outlined every cell says you are
    // on none. Either reads as a control and answers nothing.
    CHECK(filled == (p == model.page ? 1 : 0));
    CHECK(outlined == (p == model.page ? 0 : 1));

    // And it says which page it is, in words. This is the whole reason the
    // marks changed: the folder resumes on the page it was left on, so the row
    // in position two is a different game on each visit, and "which page is
    // this" has to be answerable before any tap is safe.
    char number[toybox::kIntTextChars];
    std::snprintf(number, sizeof(number), "%d", p + 1);
    CHECK(menu.target.drew(number));
  }

  // Said twice, and the second time in the header, where the eye already is
  // while it is on the rows. The bar sits at the bottom of an 800px panel; a
  // cold tester did not misread it, they never looked at it.
  //
  // Composed rather than written out, so the strings cannot go stale the first
  // time a game is added and the folder gains a page.
  char onFirst[12];
  char onSecond[12];
  std::snprintf(onFirst, sizeof(onFirst), "1/%d", model.pageCount);
  std::snprintf(onSecond, sizeof(onSecond), "2/%d", model.pageCount);
  CHECK(menu.target.drew(onFirst));

  // The count moves with the page. A header that always says 1/N is worse than
  // no header at all.
  shelfui::MenuModel second = model;
  second.page = 1;
  Rendered later;
  buildShelf(later, second);
  CHECK(later.target.drew(onSecond));
  CHECK(!later.target.drew(onFirst));

  // A folder that fits draws no bar and no counter: "1/1" is furniture.
  shelfui::MenuModel lone = model;
  lone.count = 3;
  lone.page = 0;
  lone.pageCount = 1;
  Rendered single;
  buildShelf(single, lone);
  CHECK(!single.target.drew("1/1"));
}

// A row on a restored page opens ITS OWN game, not the game at that position on
// page one.
//
// The screen is handed one page as a slice, so the row a tap lands on is
// page-relative while the game it stands for is absolute. Kept as its own test
// because every other shelf tap test runs on page one, where the two are the
// same number and an off-by-a-page cannot show.
void testARowOnARestoredPageOpensItsOwnGame() {
  fui::ListItem items[3] = {};
  const char* titles[3] = {"XKCD", "SOLITAIRE", "BATTLESHIP"};
  for (int i = 0; i < 3; ++i) {
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(8 + i);
  }

  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.playerName = "SPIKY GRIM BEARD";
  model.items = items;
  model.count = 3;
  model.page = 1;
  model.pageCount = 3;

  Rendered menu;
  buildShelf(menu, model);

  const int rowY = toybox::kHeaderHeight + toybox::kGutter * 3 + toybox::kRowHeight + toybox::kRowHeight / 2;
  const fui::ActionEvent hit = menu.tap(240, rowY);
  CHECK(hit.action == shelfui::ActionOpen);
  CHECK(hit.value == 9);

  // And nothing on a restored page is marked. This is the page the mark used to
  // live on -- it existed to explain why the list had not opened at the top --
  // so it is the page where a reintroduced cursor would show first.
  for (const auto& run : menu.target.texts) {
    for (const char* title : titles) {
      if (run.text == title) CHECK(run.color == fui::Color::Black);
    }
  }
}

void testAFolderWithoutADeviceNameHasNoFooter() {
  fui::ListItem items[1] = {};
  items[0].label = "STUDY";

  shelfui::MenuModel model;
  model.title = "SHELF";
  model.items = items;
  model.count = 1;
  // APPS does not show the device name: it exists for playing against somebody
  // in the room, and here it would be a word with no job.
  model.playerName = nullptr;

  Rendered menu;
  buildShelf(menu, model);
  CHECK(menu.target.drew("STUDY"));
  CHECK(!menu.target.drew("SPIKY GRIM BEARD"));

  // Not drawing the name is not enough: the control must not be there at all.
  // A footer built from a null label draws nothing visible, so an assertion on
  // the text alone passes while an invisible door to PLAYER sits at the bottom
  // of the screen waiting to be pressed. Tap where it would be.
  const int footerY = 800 - toybox::kMargin - toybox::kRowHeight / 2;
  CHECK(menu.tap(240, footerY).action != shelfui::ActionOpenPlayer);
  // And nothing painted a face there either. The bar is gone, not blanked.
  CHECK(menu.target.blits.empty());

  // The footer is not just hidden, its space is returned to the list. A folder
  // that reserved room for a control it never draws is dead space, and the list
  // would think it had one row less than it does.
  const fui::Rect withName = shelfui::listBand(device(), true, false);
  const fui::Rect without = shelfui::listBand(device(), false, false);
  CHECK(without.height > withName.height);
  CHECK(without.height - withName.height == toybox::kRowHeight + toybox::kGutter);
}

void testTheShelfFooterIsADoorWithAFaceOnIt() {
  fui::ListItem items[1] = {};
  items[0].label = "SOLITAIRE";

  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.items = items;
  model.count = 1;
  model.playerName = "PUNK SLY GOATEE";

  Rendered menu;
  buildShelf(menu, model);

  const FakeTarget::TextRun* bar = menu.target.find("PUNK SLY GOATEE");
  CHECK(bar != nullptr);
  if (bar == nullptr) return;

  // It opens PLAYER. It used to reroll in place, which meant the only way to
  // look at your name was also the only way to lose it.
  const fui::ActionEvent event = menu.tap(240, bar->rect.y + bar->rect.height / 2);
  CHECK(event.action == shelfui::ActionOpenPlayer);
  // Both edges, because a bar this wide is exactly where a hit region computed
  // separately from the paint goes dead at the ends -- which is how PLAY AGAIN
  // shipped with dead outer thirds.
  CHECK(menu.tap(toybox::kMargin + 2, bar->rect.y + bar->rect.height / 2).action == shelfui::ActionOpenPlayer);
  CHECK(menu.tap(480 - toybox::kMargin - 2, bar->rect.y + bar->rect.height / 2).action == shelfui::ActionOpenPlayer);

  // The face is the name's face, drawn in paper. This bar is filled solid
  // black, so a face in ink would be perfectly invisible and nothing would say
  // so -- the multiplayer mark went black-on-black once for exactly this
  // reason, and then white-on-white when it moved.
  const player::Avatar face = player::avatarFor("PUNK SLY GOATEE", player::AvatarSize::Row);
  const int16_t size = player::avatarPixels(player::AvatarSize::Row);
  const fui::Rect paper = menu.target.faceRect(face, fui::Color::White);
  CHECK(paper.width == size && paper.height == size);
  CHECK(menu.target.faceRect(face, fui::Color::Black).width == 0);
  // Inside the bar, and at its left.
  CHECK(paper.x >= toybox::kMargin);
  CHECK(paper.bottom() <= 800 - toybox::kMargin);

  // The name gets a band of its own that touches neither the face nor the
  // chevron. This is asserted as geometry rather than as "the face is in the
  // left quarter", which is what the previous version checked and why it passed
  // while the widest name ran straight through both marks: the label was handed
  // to the button, the button centred it across the whole bar, and the fake
  // font here is narrower than the real one so nothing collided in the test.
  //
  // Three things cannot share one centre line. Comparing the rects compares
  // what was actually drawn, at any font.
  const fui::Rect chevron = menu.target.blits.back().rect;
  CHECK(chevron.x > bar->rect.x);
  CHECK(bar->rect.x >= paper.right());
  CHECK(bar->rect.right() <= chevron.x);
  // ...and with air, not merely abutting.
  CHECK(bar->rect.x - paper.right() >= toybox::kGutter);
  CHECK(chevron.x - bar->rect.right() >= toybox::kGutter);
}

// --- PLAYER ----------------------------------------------------------------

void buildPlayer(Rendered& out, const playerui::PlayerModel& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  playerui::buildPlayer(screen, model);
}

playerui::PlayerModel playerModel() {
  playerui::PlayerModel model;
  model.name = "SPIKY GRIM BEARD";
  model.words[0] = "SPIKY";
  model.words[1] = "GRIM";
  model.words[2] = "BEARD";
  return model;
}

void testPlayerOffersThreeSeparateWords() {
  Rendered out;
  buildPlayer(out, playerModel());

  CHECK(out.target.drew("PLAYER"));
  CHECK(out.target.drew("SPIKY"));
  CHECK(out.target.drew("GRIM"));
  CHECK(out.target.drew("BEARD"));
  CHECK(out.target.drew("BACK"));
  CHECK(!out.interactions.overflowed());

  // The name is not spelled out a second time. Two copies of one string are two
  // things that can disagree, and the words already read as the name.
  CHECK(!out.target.drew("SPIKY GRIM BEARD"));

  // Each word rolls its own slot and nothing else. One action carrying the slot
  // as its value, so a fourth slot would need no new branch -- but the values
  // have to actually differ, or all three buttons roll the hair.
  const char* words[3] = {"SPIKY", "GRIM", "BEARD"};
  for (int slot = 0; slot < 3; ++slot) {
    const FakeTarget::TextRun* run = out.target.find(words[slot]);
    CHECK(run != nullptr);
    if (run == nullptr) continue;
    const fui::ActionEvent event = out.tap(run->rect.x + run->rect.width / 2, run->rect.y + run->rect.height / 2);
    CHECK(event.action == playerui::ActionStepSlot);
    CHECK(event.value == slot);
  }
}

void testPlayerWordsTileTheRowWithoutGapsOrOverlap() {
  Rendered out;
  buildPlayer(out, playerModel());

  const char* words[3] = {"SPIKY", "GRIM", "BEARD"};
  const FakeTarget::TextRun* runs[3] = {};
  for (int slot = 0; slot < 3; ++slot) runs[slot] = out.target.find(words[slot]);
  CHECK(runs[0] != nullptr && runs[1] != nullptr && runs[2] != nullptr);
  if (runs[0] == nullptr || runs[1] == nullptr || runs[2] == nullptr) return;

  // Left to right in slot order, which is the whole reading of the name.
  CHECK(runs[0]->rect.x < runs[1]->rect.x);
  CHECK(runs[1]->rect.x < runs[2]->rect.x);
  CHECK(runs[0]->rect.y == runs[1]->rect.y && runs[1]->rect.y == runs[2]->rect.y);

  // Sweep the whole band a pixel at a time and ask what each column does. This
  // is the assertion, rather than comparing rect edges, because what a player
  // hits is the routed action and the label's rect is inset from the control
  // that owns it. Three across a fixed band is where integer division shows up:
  // the last one ends short of the margin, or two overlap and one swallows the
  // other's taps.
  const int y = runs[0]->rect.y + runs[0]->rect.height / 2;
  // Right() is exclusive, so the last column inside the band is one short of
  // the margin.
  const int lastColumn = 480 - toybox::kMargin - 1;
  int owner[481];
  for (int x = toybox::kMargin; x <= lastColumn; ++x) {
    const fui::ActionEvent event = out.tap(x, y);
    owner[x] = event.action == playerui::ActionStepSlot ? event.value : -1;
  }

  // Both outer edges of the band belong to the outer words: no dead margin.
  CHECK(owner[toybox::kMargin] == 0);
  CHECK(owner[lastColumn] == 2);
  // Every slot owns a contiguous run, in order, and nothing owns two runs.
  int transitions = 0;
  int deadColumns = 0;
  int outOfOrder = 0;
  int lastOwner = 0;
  for (int x = toybox::kMargin; x <= lastColumn; ++x) {
    if (owner[x] < 0) {
      deadColumns++;
      continue;
    }
    if (owner[x] != lastOwner) {
      transitions++;
      if (owner[x] < lastOwner) outOfOrder++;
      lastOwner = owner[x];
    }
  }
  CHECK(transitions == 2);
  CHECK(outOfOrder == 0);
  // Only the two gutters may be untappable, and only if the controls do not
  // already cover them.
  CHECK(deadColumns <= 2 * toybox::kGutter);
}

void testPlayerDrawsTheFaceItsNameDescribes() {
  Rendered out;
  buildPlayer(out, playerModel());

  const player::Avatar face = player::avatarFor("SPIKY GRIM BEARD", player::AvatarSize::Portrait);
  const fui::Rect drawn = out.target.faceRect(face, fui::Color::Black);

  // Every layer on one rect, exactly kFaceSize, horizontally centred in the
  // content band. The sampler is nearest-neighbour, so an integer multiple of
  // the 120px asset doubles every pixel evenly and anything else leaves some
  // strokes a pixel fatter than their neighbours.
  CHECK(drawn.width == playerui::kFaceSize && drawn.height == playerui::kFaceSize);
  CHECK(drawn.x == toybox::kMargin + (480 - 2 * toybox::kMargin - playerui::kFaceSize) / 2);
  CHECK(drawn.y > toybox::kHeaderHeight);
  CHECK(playerui::kFaceSize % player::avatarPixels(player::AvatarSize::Portrait) == 0);

  // A different name is a different face. Without this the whole feature could
  // be one static drawing and every assertion above would still pass.
  Rendered other;
  playerui::PlayerModel changed = playerModel();
  changed.name = "BALD GLAD GRIN";
  changed.words[0] = "BALD";
  changed.words[1] = "GLAD";
  changed.words[2] = "GRIN";
  buildPlayer(other, changed);
  const player::Avatar theirs = player::avatarFor("BALD GLAD GRIN", player::AvatarSize::Portrait);
  CHECK(other.target.faceRect(theirs, fui::Color::Black).width == playerui::kFaceSize);
  CHECK(face.layer[1] != theirs.layer[1]);
  CHECK(face.layer[2] != theirs.layer[2]);
  CHECK(face.layer[3] != theirs.layer[3]);
  // The first face is not on this screen at all: the eyes and mouth it named
  // are gone, not merely overdrawn.
  CHECK(other.target.faceRect(face, fui::Color::Black).width == 0);
}

void testPlayerBackLeaves() {
  Rendered out;
  buildPlayer(out, playerModel());
  const FakeTarget::TextRun* back = out.target.find("BACK");
  CHECK(back != nullptr);
  if (back == nullptr) return;
  CHECK(out.tap(back->rect.x + back->rect.width / 2, back->rect.y + back->rect.height / 2).action ==
        playerui::ActionLeavePlayer);
  // The face is not a button. It is the biggest thing on the screen, so a
  // stray hit region over it would swallow most taps aimed at nothing.
  CHECK(out.tap(240, toybox::kHeaderHeight + toybox::kGutter * 4 + playerui::kFaceSize / 2).action == fui::NO_ACTION);
}

// --- the artwork and the vocabulary ----------------------------------------

void testEveryWordHasTheArtworkItNames() {
  // Two hand-maintained lists in two files: the words in PlayerName.cpp and the
  // bitmaps in PlayerAvatar.cpp. A static_assert pins their lengths. Nothing
  // but this pins their ORDER, and getting that wrong is silent -- swap two
  // hair words and every device quietly grows different hair, with no build
  // error and no visible defect until somebody who knows their own name looks
  // at their own face.
  int mismatched = 0;
  for (int slot = 0; slot < player::kSlotCount; ++slot) {
    for (uint8_t index = 0; index < player::wordCount(slot); ++index) {
      const char* word = player::word(slot, index);
      const char* art = player::artWord(slot, index);
      if (word == nullptr || art == nullptr || std::strcmp(word, art) != 0) mismatched++;
    }
  }
  CHECK(mismatched == 0);

  // Every triple resolves to a full face at both sizes, so no combination has a
  // hole in it.
  int incomplete = 0;
  for (uint8_t hair = 0; hair < player::wordCount(player::SlotHair); ++hair) {
    for (uint8_t eyes = 0; eyes < player::wordCount(player::SlotEyes); ++eyes) {
      for (uint8_t mouth = 0; mouth < player::wordCount(player::SlotMouth); ++mouth) {
        player::Name name;
        name.word[player::SlotHair] = hair;
        name.word[player::SlotEyes] = eyes;
        name.word[player::SlotMouth] = mouth;
        for (const player::AvatarSize size : {player::AvatarSize::Row, player::AvatarSize::Portrait}) {
          const player::Avatar avatar = player::avatarFor(name, size);
          // Four layers for every triple, including BALD -- its drawing is
          // deliberately empty, but it is a drawing, so the table has no holes
          // and the draw loop has no special case.
          for (int layer = 0; layer < player::Avatar::kLayerCount; ++layer) {
            if (avatar.layer[layer] == nullptr) incomplete++;
          }
        }
      }
    }
  }
  CHECK(incomplete == 0);
}

void testAnUnreadableNameDrawsThePlainHead() {
  // What a device running a different word list sends. It must come out as the
  // portrait everyone starts from, not as the wrong person and not as nothing.
  // PEERING is seven letters, so no future list can contain it -- the 20-char
  // name budget caps a word at six. A sample built from a word that happens not
  // to exist yet stops testing anything the day somebody adds it, which is what
  // happened to the previous one when CROSS became a real pair of eyes.
  const player::Avatar stranger = player::avatarFor("MOHAWK PEERING BEARD", player::AvatarSize::Row);
  CHECK(stranger.layer[0] != nullptr);
  CHECK(stranger.layer[1] == nullptr);
  CHECK(stranger.layer[2] == nullptr);
  // The third word IS one of ours, and a name we can half read draws the half
  // we understand rather than being thrown away whole.
  CHECK(stranger.layer[3] != nullptr);

  const player::Avatar nobody = player::avatarFor("", player::AvatarSize::Row);
  CHECK(nobody.layer[0] != nullptr);
  for (int i = 1; i < player::Avatar::kLayerCount; ++i) CHECK(nobody.layer[i] == nullptr);
}

void testBothSeatsWearTheirOwnFace() {
  // The payoff, and the reason the avatar is derived rather than stored: their
  // name already crossed the radio, so their face costs no wire bytes and
  // cannot arrive stale.
  Rendered out;
  linkui::LinkModel model = searchingModel();
  // Your seat is LABELLED "YOU" and drawn from your NAME. Those are two fields
  // on purpose, and this is the case that proves it: the first version derived
  // the face from the label, so every player saw a blank head in their own seat
  // -- "YOU" parses to no words at all. Nothing failed, nothing logged, and the
  // test passed because it had helpfully put a real name in the label.
  model.yourName = "YOU";
  model.yourFaceName = "SPIKY GRIM BEARD";
  model.theirName = "BALD SPECS GRIN";
  model.them = linkui::SeatState::Ready;
  model.linked = true;
  buildLink(out, model);

  CHECK(out.target.drew("YOU"));
  CHECK(!out.target.drew("SPIKY GRIM BEARD"));

  const player::Avatar mine = player::avatarFor("SPIKY GRIM BEARD", player::AvatarSize::Row);
  const player::Avatar theirs = player::avatarFor("BALD SPECS GRIN", player::AvatarSize::Row);
  // Different names, so at least one layer differs -- otherwise this test would
  // pass on a screen that drew the same face twice.
  CHECK(mine.layer[1] != theirs.layer[1]);

  int mineDrawn = 0;
  int theirsDrawn = 0;
  for (const auto& blit : out.target.blits) {
    for (int i = 0; i < player::Avatar::kLayerCount; ++i) {
      if (mine.layer[i] != nullptr && blit.data == mine.layer[i]->bits) mineDrawn++;
      if (theirs.layer[i] != nullptr && blit.data == theirs.layer[i]->bits) theirsDrawn++;
    }
  }
  // The base is shared, so it lands twice; each face's own layers land once.
  CHECK(mineDrawn == out.target.layersOf(mine) + 1);
  CHECK(theirsDrawn == out.target.layersOf(theirs) + 1);

  // An empty seat still gets a head: "somebody will be here" is what LOOKING
  // means, and the plain portrait says it without a special case.
  Rendered searching;
  buildLink(searching, searchingModel());
  const player::Avatar vacant = player::avatarFor("", player::AvatarSize::Row);
  CHECK(searching.target.layersOf(vacant) == 1);
  int vacantDrawn = 0;
  for (const auto& blit : searching.target.blits) {
    if (blit.data == vacant.layer[0]->bits) vacantDrawn++;
  }
  // Both seats: yours (MARIO, which parses to nothing) and the empty one.
  CHECK(vacantDrawn == 2);
}

// --- Hacker News -----------------------------------------------------------

void buildHnReader(Rendered& out, const hnui::ReaderModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::ReaderBody body;
  body.text = out.bodyText;
  body.style = toybox::themeTokens().bodyText;
  body.wrap = &out.wrap;
  hnui::buildReader(screen, model, body);
}

void buildHnNotice(Rendered& out, const hnui::NoticeModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildNotice(screen, model);
}

hnui::ReaderModel articleModel() {
  hnui::ReaderModel model;
  model.title = "A tiny e-ink game console";
  // set on the Rendered by the caller; see buildInstaReader/buildHnReader
  model.pageLabel = "1/3";
  model.showingComments = false;
  model.swapAvailable = true;
  model.canPagePrev = false;
  model.canPageNext = true;
  return model;
}

bool drewText(const Rendered& out, const char* needle) {
  for (const auto& run : out.target.texts) {
    if (run.text.find(needle) != std::string::npos) return true;
  }
  return false;
}

void testHnReaderFooter() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.canPagePrev = true;
  buildHnReader(out, model);

  // The middle button says where it goes, and it is the wide one because it is
  // the only control here that changes what is being read.
  CHECK(drewText(out, "COMMENTS"));
  CHECK(drewText(out, "1/3"));

  // Find the footer row and tap the far edges of each control. This is the
  // PLAY AGAIN bug class: a button whose painted width and hit rect disagree is
  // dead on its edges and looks perfectly fine in a screenshot.
  const fui::Rect body = hnui::readerBody(device());
  const int footerY = body.y + body.height + 24;

  bool sawPrev = false;
  bool sawNext = false;
  bool sawSwap = false;
  for (int x = 0; x < 480; ++x) {
    const fui::ActionEvent event = out.tap(x, footerY);
    if (event.action == hnui::ActionPagePrev) sawPrev = true;
    if (event.action == hnui::ActionPageNext) sawNext = true;
    if (event.action == hnui::ActionSwapView) sawSwap = true;
  }
  CHECK(sawPrev);
  CHECK(sawNext);
  CHECK(sawSwap);
}

void testHnReaderDisabledControls() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.canPagePrev = false;  // page one: there is nowhere back to go
  model.canPageNext = false;
  buildHnReader(out, model);

  const fui::Rect body = hnui::readerBody(device());
  const int footerY = body.y + body.height + 24;
  for (int x = 0; x < 480; ++x) {
    const fui::ActionEvent event = out.tap(x, footerY);
    // A dimmed control keeps its place in the bar so nothing moves, but it must
    // not fire. Dimming is drawn in the fill, because there is no grey text on
    // this panel and a coloured label would just draw solid black.
    CHECK(event.action != hnui::ActionPagePrev);
    CHECK(event.action != hnui::ActionPageNext);
  }
}

void testHnReaderSwapLabelFollowsMode() {
  Rendered article;
  buildHnReader(article, articleModel());
  CHECK(drewText(article, "COMMENTS"));
  CHECK(!drewText(article, "ARTICLE  "));

  Rendered comments;
  hnui::ReaderModel model = articleModel();
  model.showingComments = true;
  buildHnReader(comments, model);
  // One action, and the model decides which way it points, so the label and the
  // effect cannot disagree.
  CHECK(drewText(comments, "ARTICLE"));
}

void testHnReaderTextStaysInItsRect() {
  Rendered out;
  buildHnReader(out, articleModel());

  // The Activity pages by counting the lines that fit in readerBody(). If the
  // text were drawn anywhere else, a page turn would skip or repeat lines and
  // nothing would report it.
  const fui::Rect body = hnui::readerBody(device());
  bool sawBodyText = false;
  for (const auto& run : out.target.texts) {
    if (run.text.find("Some words") == std::string::npos) continue;
    sawBodyText = true;
    CHECK(run.rect.y >= body.y);
    CHECK(run.rect.y < body.y + body.height);
    CHECK(run.rect.x >= body.x);
  }
  CHECK(sawBodyText);
}

void testHnNotice() {
  Rendered unreadable;
  hnui::NoticeModel model;
  model.headline = "NOT READABLE HERE";
  model.message = "This link is not a page of text.";
  model.mark = &icon_unreadable_32;
  // Both halves of the control, because buildNotice now draws it only when both
  // are set. A label with no action is a button that answers nothing.
  model.actionLabel = "READ THE COMMENTS";
  model.action = hnui::ActionNotice;
  buildHnNotice(unreadable, model);

  CHECK(drewText(unreadable, "NOT READABLE HERE"));
  CHECK(drewText(unreadable, "READ THE COMMENTS"));

  // The mark is a 1-bpp mask painted in one colour, so it is invisible on a
  // background of that colour and nothing warns you. This one sits on paper, so
  // it has to be black; drawn white it would be a blank square nobody notices.
  bool markDrawnInInk = false;
  for (const auto& blit : unreadable.target.blits) {
    if (blit.color == fui::Color::Black) markDrawnInInk = true;
  }
  CHECK(markDrawnInInk);

  // Comments are always reachable, which is the promise this screen exists to
  // keep: the only button on it leads there.
  bool foundWayOut = false;
  for (int y = 0; y < 800; y += 4) {
    for (int x = 0; x < 480; x += 8) {
      if (unreadable.tap(x, y).action == hnui::ActionNotice) foundWayOut = true;
    }
  }
  CHECK(foundWayOut);

  // A busy notice has nothing to decide yet, so it offers no button at all.
  Rendered busy;
  hnui::NoticeModel loading;
  loading.headline = "HACKER NEWS";
  loading.message = "FETCHING THE FRONT PAGE";
  buildHnNotice(busy, loading);
  CHECK(drewText(busy, "FETCHING THE FRONT PAGE"));
  for (int y = 0; y < 800; y += 4) {
    CHECK(busy.tap(240, y).action != hnui::ActionNotice);
  }
}

// EVERY notice has a way off it, and the notice that is not about an unreadable
// link is the one that did not.
//
// This screen has no segment strip and no list under it, so a notice with no
// control is a full-screen dead end whose only exit is a left-edge swipe that
// nothing on it mentions -- with the SAVED shelf, the half of this app that
// needs no network, on the far side of it. A failed ARTICLE or THREAD fetch
// showed exactly that, and it is the common failure: on a train every tap on a
// cached front page lands there. The fix for a failed FRONT PAGE went into one
// arm of the same `if` and not into its twin.
void testHnEveryNoticeCarriesAWayOff() {
  // The rule itself, asked directly. It cannot answer "no control": that is the
  // whole reason it is a function rather than a ternary at the call site, where
  // the nullptr half quietly covered four different failures.
  for (const bool unreadable : {false, true}) {
    const hnui::NoticeControl control = hnui::noticeControl(unreadable);
    CHECK(control.label != nullptr);
    CHECK(control.action != fui::NO_ACTION);
  }
  // And the two are DIFFERENT doors. A failure screen must not offer to fetch a
  // thread over the network it has just reported down.
  CHECK(hnui::noticeControl(false).action != hnui::noticeControl(true).action);

  // Drawn, live, and legible. The failure notice as the Activity builds it: no
  // mark, the same sentence the list's own failure shows, and the control the
  // rule above hands out.
  Rendered failure;
  hnui::NoticeModel model;
  model.headline = "NO LUCK";
  model.message = "Could not reach Hacker News. Saved articles still work.";
  const hnui::NoticeControl control = hnui::noticeControl(false);
  model.actionLabel = control.label;
  model.action = control.action;
  buildHnNotice(failure, model);

  // Two questions, and the first one is the one the bug was about: does ANY
  // pixel on this screen answer a finger. Asked separately from "is it the
  // right door" because a dead end fails the first and a mis-wired control
  // fails only the second.
  //
  // The door is named by its literal id, never by control.action. Comparing a
  // tap against control.action would make a revert that answers NO_ACTION pass
  // vacuously: every blank pixel on the panel returns NO_ACTION, so the sweep
  // would find its "door" in the margin. A test derived from the value under
  // test cannot falsify it.
  bool answersAFinger = false;
  bool foundTheDoor = false;
  for (int y = 0; y < 800; y += 4) {
    for (int x = 0; x < 480; x += 8) {
      const fui::ActionId action = failure.tap(x, y).action;
      if (action != fui::NO_ACTION) answersAFinger = true;
      if (action == hnui::ActionNoticeBack) foundTheDoor = true;
    }
  }
  CHECK(answersAFinger);
  CHECK(foundTheDoor);
  // Present is not legible: a label wider than its pill is ellipsized by the
  // renderer and drewText would still find it. Guarded so that a regression
  // answering nullptr here reports as the named CHECKs above rather than as a
  // segfault, which names nothing and cannot be counted.
  if (control.label != nullptr) CHECK(drewLabelWhole(failure, control.label));

  // The pairing rule, from the side that makes the control invisible rather
  // than dead. A label with no action used to be drawable; it would paint a
  // pill that answers nothing, which is worse than no pill at all because the
  // reader tries it and concludes the screen is frozen.
  Rendered orphan;
  hnui::NoticeModel unpaired;
  unpaired.headline = "NO LUCK";
  unpaired.message = "Could not reach Hacker News. Saved articles still work.";
  unpaired.actionLabel = "BACK TO THE LIST";
  buildHnNotice(orphan, unpaired);
  CHECK(!drewText(orphan, "BACK TO THE LIST"));
}

// The save mark, identified by being the only bitmap the reader draws and NOT
// by its pointer: ToyboxIcons.h declares every icon `static` at namespace
// scope, so this file's `icon_saved_32.bits` is a different array from the
// screen builder's and a pointer comparison silently never matches.
const FakeTarget::Blit* saveMarkIn(const Rendered& out) {
  return out.target.blits.size() == 1 ? &out.target.blits[0] : nullptr;
}

// Whether a solid paper fill sits under `rect`. The chip is that fill, and
// nothing else on this screen paints one.
bool paperChipUnder(const Rendered& out, const fui::Rect& rect) {
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Paint& paint = out.target.fillPaints[i];
    if (paint.kind != fui::PaintKind::Solid || paint.color != fui::Color::White) continue;
    const fui::Rect& fill = out.target.fills[i];
    if (fill.x <= rect.x && fill.y <= rect.y && fill.x + fill.width >= rect.x + rect.width &&
        fill.y + fill.height >= rect.y + rect.height) {
      return true;
    }
  }
  return false;
}

fui::ActionEvent tapTheMark(Rendered& out, const fui::Rect& mark) {
  return out.tap(mark.x + mark.width / 2, mark.y + mark.height / 2);
}

// The thing about this mark that a screenshot cannot tell you: the header band
// is SOLID BLACK, so the two ordinary style sets swap weights on it. A black
// fill IS the band and disappears; a white fill is the loudest thing on the
// screen. Styled "filled means saved" out of those, the mark reads backwards --
// which is exactly how two cold testers read it, one of them removing an
// article they believed they had just kept.
//
// So the claim under test is about WEIGHT, not about which style was passed:
// the state carrying the paper-coloured chip has to be the saved one.
void testHnSaveMarkIsLoudestWhenSaved() {
  Rendered kept;
  hnui::ReaderModel model = articleModel();
  model.canSave = true;
  model.saved = true;
  buildHnReader(kept, model);

  const FakeTarget::Blit* keptMark = saveMarkIn(kept);
  CHECK(keptMark != nullptr);
  if (keptMark != nullptr) {
    // On the device: a paper chip with the bookmark knocked out of it.
    CHECK(paperChipUnder(kept, keptMark->rect));
    CHECK(keptMark->color == fui::Color::Black);
    CHECK(tapTheMark(kept, keptMark->rect).action == hnui::ActionUnsave);
  }
  // The glyph is one 1-bpp mask and never fills, so the chip was the only thing
  // that ever changed and nothing said what a tap had just done. A word does.
  CHECK(kept.target.drew("SAVED"));
  CHECK(!kept.target.drew("SAVE"));

  Rendered offer;
  model.saved = false;
  buildHnReader(offer, model);

  const FakeTarget::Blit* offerMark = saveMarkIn(offer);
  CHECK(offerMark != nullptr);
  if (offerMark != nullptr) {
    // The quiet state. A paper chip here is the bug: it outshouts the kept one.
    CHECK(!paperChipUnder(offer, offerMark->rect));
    // Drawn in paper so it is visible AT ALL on a black band -- the same trap
    // that made the page label invisible for two renders.
    CHECK(offerMark->color == fui::Color::White);
    CHECK(tapTheMark(offer, offerMark->rect).action == hnui::ActionSave);
  }
  CHECK(offer.target.drew("SAVE"));
  CHECK(!offer.target.drew("SAVED"));
}

// A thread carries the mark too. The stories worth keeping for a train are the
// ones whose page will not render here, and for those the conversation is the
// only thing there is to keep.
void testHnAThreadCanBeKept() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.showingComments = true;
  model.canSave = true;
  model.saved = false;
  buildHnReader(out, model);

  const FakeTarget::Blit* mark = saveMarkIn(out);
  CHECK(mark != nullptr);
  if (mark != nullptr) CHECK(tapTheMark(out, mark->rect).action == hnui::ActionSave);

  // And a reader with nothing to key an entry by draws no mark at all, rather
  // than offering a control that cannot work.
  Rendered none;
  hnui::ReaderModel unkeyed = articleModel();
  unkeyed.canSave = false;
  buildHnReader(none, unkeyed);
  CHECK(saveMarkIn(none) == nullptr);
  CHECK(!none.target.drew("SAVE"));
  CHECK(!none.target.drew("SAVED"));
}

void testHnReaderShowsWhereYouAre() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.pageLabel = "3/12";
  buildHnReader(out, model);

  // The page indicator has to be drawn in paper. The band is solid black and
  // the component takes rightLabel's style from the theme's subtitle, whose
  // colour is Black -- so a label left at the default is painted black on black
  // and is indistinguishable from never having been set. It went missing
  // through two renders exactly that way.
  bool paperOnTheBand = false;
  for (const auto& run : out.target.texts) {
    if (run.text == "3/12" && run.color == fui::Color::White) paperOnTheBand = true;
  }
  CHECK(paperOnTheBand);

  // The band carries the story's own headline, in paper for the same reason,
  // and in its own case: a title is content, not chrome. The mode word the
  // band used to shout belongs to the footer's swap button alone.
  bool headlineOnTheBand = false;
  for (const auto& run : out.target.texts) {
    if (run.text == "A tiny e-ink game console" && run.color == fui::Color::White) headlineOnTheBand = true;
  }
  CHECK(headlineOnTheBand);
  CHECK(!drewText(out, "ARTICLE"));
}

// A card that would not take a save says so OVER the reader, not by replacing
// it. Card #40: a failed save called showNotice(), which switched the Activity
// to its full-screen Notice phase and threw the reader out of the page it was
// on -- for the one failure that leaves what you were reading perfectly intact.
// buildReader now draws the refusal from the model as a transient toast, so the
// reader's own chrome is still there beneath it and nothing navigated away.
void testHnReaderSaveFailedToastStaysOnTheReader() {
  Rendered out;
  hnui::ReaderModel model = articleModel();
  model.canSave = true;
  model.saveNotice = "Not saved: the card is full.";
  buildHnReader(out, model);

  // The refusal is on the page.
  CHECK(drewText(out, "card is full"));
  // And the reader is STILL the reader underneath it: the swap control, the
  // page label and the save mark are all drawn, which a full-screen notice
  // would not carry. That is the whole of #40 -- an overlay, not a new screen.
  CHECK(drewText(out, "COMMENTS"));
  CHECK(drewText(out, "1/3"));
  CHECK(saveMarkIn(out) != nullptr);

  // The ordinary paint, with nothing refused, is clean: the toast is drawn only
  // when the model carries a reason.
  Rendered clean;
  hnui::ReaderModel plain = articleModel();
  plain.canSave = true;
  buildHnReader(clean, plain);
  CHECK(!drewText(clean, "card is full"));
}

void testHnFitLines() {
  // The fake target bills every character at 10px, so the arithmetic here is
  // exact: a 200px line holds 20 characters.
  FakeTarget target;
  fui::TextStyle style;

  const auto fit = [&](const char* text, int16_t width, int lines) {
    return hnui::fitLines(target, text, width, lines, style);
  };

  // Fits outright: returned untouched, with no ellipsis bolted on.
  CHECK(fit("Waymo in Dallas", 200, 2) == "Waymo in Dallas");
  CHECK(fit("Waymo in Dallas", 150, 1) == "Waymo in Dallas");

  // Wraps across two lines and still fits: also untouched. This is the case the
  // first implementation got wrong -- it appended the ellipsis to the whole
  // string and measured that against ONE line, so anything that wrapped was
  // trimmed back to a single line and the front page read "In Memory of My...".
  CHECK(fit("There Will Come Soft Rains", 150, 2) == "There Will Come Soft Rains");
  CHECK(fit("There Will Come Soft Rains", 150, 1) != "There Will Come Soft Rains");

  // Genuinely too long: cut on a space, never inside a word, and marked.
  const std::string cut = fit("In Memory of My Wife Elise Cawley with Thanks for Many Years", 200, 2);
  CHECK(cut.size() > 3);
  CHECK(cut.rfind("...") == cut.size() - 3);
  const std::string body = cut.substr(0, cut.size() - 3);
  // Every word kept is a whole word from the original.
  CHECK(std::string("In Memory of My Wife Elise Cawley with Thanks for Many Years").rfind(body, 0) == 0);
  CHECK(!body.empty() && body.back() != ' ');

  // Two lines really do hold more than one.
  CHECK(fit("In Memory of My Wife Elise Cawley with Thanks", 200, 2).size() >
        fit("In Memory of My Wife Elise Cawley with Thanks", 200, 1).size());

  // A single word wider than the whole line cannot be broken on a space, so it
  // is allowed through rather than looping forever hunting for a break.
  const std::string huge = fit("Supercalifragilisticexpialidocious", 100, 2);
  CHECK(!huge.empty());

  // Degenerate inputs return something drawable rather than misbehaving.
  CHECK(fit(nullptr, 200, 2).empty());
  CHECK(fit("anything", 0, 2).empty());
  CHECK(fit("anything", 200, 0).empty());
  CHECK(fit("", 200, 2).empty());
}

void testHnList() {
  Rendered out;
  fui::ListItem items[3];
  items[0].label = "First story";
  items[0].subtitle = "412 points, 88 comments";
  items[0].actionValue = 0;
  items[1].label = "Second story";
  items[1].subtitle = "12 points, 3 comments";
  items[1].actionValue = 1;
  items[2].label = "Third story";
  items[2].subtitle = "9 points, 0 comments";
  items[2].actionValue = 2;

  hnui::ListModel model;
  model.items = items;
  model.count = 3;
  model.selected = 1;

  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildList(screen, model);

  CHECK(drewText(out, "First story"));
  CHECK(drewText(out, "412 points, 88 comments"));

  // Every row opens, and each carries its own index: a row that routes the
  // wrong value opens somebody else's story.
  bool opened[3] = {false, false, false};
  const fui::Rect band = hnui::listBand(ctx);
  for (int y = band.y; y < band.y + band.height; ++y) {
    const fui::ActionEvent event = out.tap(240, y);
    if (event.action == hnui::ActionOpenStory && event.value >= 0 && event.value < 3) opened[event.value] = true;
  }
  CHECK(opened[0]);
  CHECK(opened[1]);
  CHECK(opened[2]);

  // An empty front page says so rather than drawing a blank panel.
  Rendered empty;
  hnui::ListModel none;
  toybox::Frame emptyFrame(empty.target, ctx, noInput, empty.interactions);
  toybox::Screen emptyScreen(emptyFrame, toybox::themeTokens());
  hnui::buildList(emptyScreen, none);
  CHECK(drewText(empty, "NOTHING TO READ"));

  // An empty SAVED shelf is the ordinary state of a new device, and both lines
  // of it have to be IN INK. The display cut's token colour is paper because it
  // is otherwise only ever set on the black band, so a headline taken straight
  // from the theme lands white on white paper and the shelf answers with one
  // small sentence and an expanse of nothing.
  Rendered shelf;
  hnui::ListModel nothingSaved;
  nothingSaved.title = "SAVED";
  nothingSaved.showingSaved = true;
  nothingSaved.emptyHeadline = "NOTHING SAVED YET";
  nothingSaved.emptyMessage = "Tap SAVE while you read.";
  toybox::Frame shelfFrame(shelf.target, ctx, noInput, shelf.interactions);
  toybox::Screen shelfScreen(shelfFrame, toybox::themeTokens());
  hnui::buildList(shelfScreen, nothingSaved);
  const FakeTarget::TextRun* headline = shelf.target.find("NOTHING SAVED YET");
  CHECK(headline != nullptr);
  if (headline != nullptr) CHECK(headline->color == fui::Color::Black);
  const FakeTarget::TextRun* line = shelf.target.find("Tap SAVE while you read.");
  CHECK(line != nullptr);
  if (line != nullptr) CHECK(line->color == fui::Color::Black);
  // And they must not be drawn ON each other. centeredText centres in the
  // content rect and consumes nothing, so two calls land on the same y: the
  // headline was painted over the sentence for as long as it was invisible.
  if (headline != nullptr && line != nullptr) {
    CHECK(headline->rect.y + headline->rect.height <= line->rect.y);
  }
}

// The empty front page is the screen a device that has never joined a network
// opens on, so it is the one that has to carry a way onward. Text alone will
// not do: an empty shelf and an unloaded front page are the same expanse of
// paper, and a live control drawn like a dead one is one nobody tries.
void testHnEmptyFrontPageOffersAWayOnward() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  Rendered cold;
  hnui::ListModel model;
  model.emptyHeadline = "NOT LOADED YET";
  model.emptyMessage = "The front page needs a connection. Saved articles do not.";
  model.emptyActionLabel = "LOAD";
  model.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame coldFrame(cold.target, ctx, noInput, cold.interactions);
  toybox::Screen coldScreen(coldFrame, toybox::themeTokens());
  hnui::buildList(coldScreen, model);

  CHECK(drewText(cold, "NOT LOADED YET"));
  // Whole, not merely present. drewText sees the string the builder HANDED the
  // renderer and the renderer is what shortens it, so a pill too narrow for its
  // own label passes every "did it draw?" check while the panel says "LO...".
  // This control is the only way off the screen a device with no network opens
  // on, so it is the last label in the app that can afford to be a guess.
  CHECK(drewLabelWhole(cold, "LOAD"));

  // The control is reachable by a finger, which is the only thing that makes it
  // a control. Swept rather than tapped at one guessed point.
  bool foundLoad = false;
  for (int y = 0; y < ctx.height; ++y) {
    if (cold.tap(240, y).action == hnui::ActionLoadFrontPage) foundLoad = true;
  }
  CHECK(foundLoad);

  // It must not sit on the segment strip. The segments are the map between the
  // two shelves, and a control stealing their taps would strand the reader on
  // the half that needs the network. Swept across the panel rather than down one
  // column, because the segments are half-width and the control is not.
  int loadBottom = -1;
  int savedTop = ctx.height;
  bool foundSaved = false;
  for (const int x : {60, 240, 380}) {
    for (int y = 0; y < ctx.height; ++y) {
      const fui::ActionEvent event = cold.tap(static_cast<int16_t>(x), static_cast<int16_t>(y));
      if (event.action == hnui::ActionLoadFrontPage && y > loadBottom) loadBottom = y;
      if (event.action == hnui::ActionShowSaved) {
        foundSaved = true;
        if (y < savedTop) savedTop = y;
      }
    }
  }
  CHECK(foundSaved);
  CHECK(loadBottom >= 0);
  CHECK(loadBottom < savedTop);
  CHECK(!cold.interactions.overflowed());

  // The same screen after a failed fetch is a DIFFERENT screen and still
  // carries the control. This is where the app used to put a full-screen notice
  // with no segments and no buttons, so a failed front page was a dead end with
  // the offline shelf on the other side of it.
  Rendered failed;
  hnui::ListModel retry;
  retry.emptyHeadline = "NO LUCK";
  retry.emptyMessage = "Could not reach Hacker News. Saved articles still work.";
  retry.emptyActionLabel = "TRY AGAIN";
  retry.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame failedFrame(failed.target, ctx, noInput, failed.interactions);
  toybox::Screen failedScreen(failedFrame, toybox::themeTokens());
  hnui::buildList(failedScreen, retry);
  CHECK(drewText(failed, "NO LUCK"));
  CHECK(drewLabelWhole(failed, "TRY AGAIN"));
  bool foundRetry = false;
  for (int y = 0; y < ctx.height; ++y) {
    if (failed.tap(240, y).action == hnui::ActionLoadFrontPage) foundRetry = true;
  }
  CHECK(foundRetry);

  // And the empty SAVED shelf carries NO such control: it is the half that
  // needs no network, and the only thing a button there could do is fetch the
  // other half.
  Rendered shelf;
  hnui::ListModel nothingSaved;
  nothingSaved.title = "SAVED";
  nothingSaved.showingSaved = true;
  nothingSaved.emptyHeadline = "NOTHING SAVED YET";
  nothingSaved.emptyMessage = "Tap SAVE while you read.";
  toybox::Frame shelfFrame(shelf.target, ctx, noInput, shelf.interactions);
  toybox::Screen shelfScreen(shelfFrame, toybox::themeTokens());
  hnui::buildList(shelfScreen, nothingSaved);
  for (int y = 0; y < ctx.height; ++y) {
    CHECK(shelf.tap(240, y).action != hnui::ActionLoadFrontPage);
  }
}

// The block stacks: headline, sentence, control, none of them on each other.
// The sentence used to reserve one line however long it was, so the line that
// has to explain what still works with no network was ellipsised at the panel
// edge with nothing to say it had been.
void testHnEmptyStateStacksWithoutOverlap() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};

  Rendered out;
  hnui::ListModel model;
  model.emptyHeadline = "NOT LOADED YET";
  // Long enough to need two lines at every plausible cut, which is the case the
  // single reserved line got wrong.
  model.emptyMessage = "The front page needs a connection. Saved articles do not.";
  model.emptyActionLabel = "LOAD";
  model.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  hnui::buildList(screen, model);

  const FakeTarget::TextRun* headline = out.target.find("NOT LOADED YET");
  const FakeTarget::TextRun* message = out.target.find(model.emptyMessage);
  const FakeTarget::TextRun* label = out.target.find("LOAD");
  CHECK(headline != nullptr);
  CHECK(message != nullptr);
  CHECK(label != nullptr);
  if (headline != nullptr && message != nullptr) {
    CHECK(headline->rect.y + headline->rect.height <= message->rect.y);
  }
  if (message != nullptr && label != nullptr) {
    CHECK(message->rect.y + message->rect.height <= label->rect.y);
  }
  // The sentence is given the lines the SDK's own wrap will emit into it. A
  // rect one line tall for a two-line sentence is a silent truncation: the SDK
  // ellipsizes and logs nothing, which is how "Tap the mark on an article to
  // ke" shipped -- and estimating the count from a single-line width divided by
  // the column is one short whenever the wrap cannot fill a line, which is how
  // "Saved articles do ..." reached a render on this very screen.
  //
  // Measured with the CAP LIFTED, which is the only version of this check that
  // can fail. See uncappedWrappedHeight: the builder reserves
  // measureWrappedText(style) and this used to assert against
  // measureWrappedText(style), so it restated the production expression and
  // went green on the exact case it names -- a wording longer than maxLines,
  // clipped and ellipsized, with the reserved rect matching the clipped
  // measurement perfectly.
  if (message != nullptr) {
    CHECK(message->rect.height >= uncappedWrappedHeight(out.target, *message));
  }
  if (headline != nullptr) {
    CHECK(headline->rect.height >= uncappedWrappedHeight(out.target, *headline));
  }

  // And with no sentence between them, the control still clears the headline by
  // the gap it is supposed to sit below by. The stack used to step over the
  // message's height whether or not a message had been drawn, so the button
  // landed a bare gutter below the TOP of the headline -- the same compositing
  // bug that already put this headline on top of its own sentence.
  //
  // Measured against the BUTTON'S HIT RECT and against the full intended
  // clearance, not against "does it overlap". Overlap is not expressible here:
  // FakeTarget's line height is smaller than the gutter, so the broken layout
  // draws them apart on this target and on top of each other on the panel,
  // where the display cut is more than twice as tall.
  Rendered bare;
  hnui::ListModel terse;
  terse.emptyHeadline = "NOT LOADED YET";
  terse.emptyMessage = nullptr;
  terse.emptyActionLabel = "LOAD";
  terse.emptyAction = hnui::ActionLoadFrontPage;
  toybox::Frame bareFrame(bare.target, ctx, noInput, bare.interactions);
  toybox::Screen bareScreen(bareFrame, toybox::themeTokens());
  hnui::buildList(bareScreen, terse);
  const FakeTarget::TextRun* bareHeadline = bare.target.find("NOT LOADED YET");
  CHECK(bareHeadline != nullptr);
  int buttonTop = ctx.height;
  for (int y = 0; y < ctx.height; ++y) {
    if (bare.tap(240, static_cast<int16_t>(y)).action == hnui::ActionLoadFrontPage && y < buttonTop) buttonTop = y;
  }
  CHECK(buttonTop < ctx.height);
  if (bareHeadline != nullptr) {
    CHECK(buttonTop >= bareHeadline->rect.y + bareHeadline->rect.height + toybox::kGutter * 2);
  }
}

// --- the study deck screen -------------------------------------------------

void buildStudyDeck(Rendered& out, const studyui::DeckModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  studyui::buildDeck(screen, model);
}

studyui::DeckModel deckWithWork(const int* forecast) {
  studyui::DeckModel model;
  model.name = "Mandarin: Vocabulary";
  model.due = 289;
  model.fresh = 4700;
  model.total = 5001;
  model.forecast = forecast;
  return model;
}

void testStudyDeckLeadsWithTheCount() {
  int forecast[studyui::kForecastDays] = {289, 4, 0, 12, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0};
  Rendered out;
  buildStudyDeck(out, deckWithWork(forecast));

  // The headline is the number, because that is the only question the screen
  // is answering when you open it.
  CHECK(out.target.drew("4989 TO GO"));
  CHECK(out.target.drew("289 DUE   4700 NEW"));
  CHECK(out.target.drew("Mandarin: Vocabulary   5001 CARDS"));
  CHECK(out.target.drew("START REVIEWING"));

  // The caption carries the number scheduled ahead. Without it an all-backlog
  // deck draws an empty panel that reads as a panel that failed.
  CHECK(out.target.drew("2 WEEKS BACK   TODAY   20 DUE AHEAD"));
}

void testStudyHeadlineIsTheHitTarget() {
  int forecast[studyui::kForecastDays] = {};
  forecast[0] = 5;
  Rendered out;
  buildStudyDeck(out, deckWithWork(forecast));

  // The most common action must be a tap on the largest thing on the screen,
  // not on a button beside it. Tapping the headline block starts the session.
  const auto* headline = out.target.find("4989 TO GO");
  CHECK(headline != nullptr);
  if (headline != nullptr) {
    const fui::ActionEvent onHeadline = out.tap(headline->rect.x + 20, headline->rect.y + 10);
    CHECK(onHeadline.action == studyui::ActionStudy);
  }

  // And the bottom door does the same thing, so the two cannot drift apart.
  const auto* door = out.target.find("START REVIEWING");
  CHECK(door != nullptr);
  if (door != nullptr) {
    const fui::ActionEvent onDoor = out.tap(door->rect.x + 20, door->rect.y + 10);
    CHECK(onDoor.action == studyui::ActionStudy);
  }
}

void testStudyDeckRowSwitchesOnlyWhenThereIsSomewhereToGo() {
  int forecast[studyui::kForecastDays] = {};
  forecast[0] = 5;

  // One deck: no switcher door. A control that cycles through one thing is a
  // control that does nothing, and drawing it would advertise a feature the
  // card does not have.
  {
    Rendered out;
    buildStudyDeck(out, deckWithWork(forecast));
    CHECK(!out.target.drew("CHANGE DECK"));
  }

  // More than one: a third door beside START REVIEWING and SYNC. It says the
  // position rather than the name (the name is the row above the ornament),
  // and tapping it is the switch -- value 3 on the shared study action.
  {
    Rendered out;
    studyui::DeckModel model = deckWithWork(forecast);
    model.deckIndex = 1;
    model.deckCount = 3;
    buildStudyDeck(out, model);
    const auto* row = out.target.find("CHANGE DECK");
    CHECK(row != nullptr);
    CHECK(out.target.drew("2 OF 3"));
    if (row != nullptr) {
      const fui::ActionEvent onRow = out.tap(row->rect.x + 20, row->rect.y + 10);
      CHECK(onRow.action == studyui::ActionStudy);
      CHECK(onRow.value == 3);
    }
  }
}

void testStudyOffersNothingWhenNothingIsDue() {
  int forecast[studyui::kForecastDays] = {};
  studyui::DeckModel model;
  model.name = "Mandarin";
  model.total = 5001;
  model.forecast = forecast;
  model.reviewed = 40;
  model.recalled = 34;
  model.sessionOver = true;

  Rendered out;
  buildStudyDeck(out, model);

  // Finishing is a state of the same screen, not a separate page: the session
  // result replaces the due counts and the door stops offering.
  CHECK(out.target.drew("DONE"));
  CHECK(out.target.drew("40 REVIEWED   85% RIGHT"));
  CHECK(out.target.drew("NOTHING TO REVIEW"));
  CHECK(!out.target.drew("START REVIEWING"));

  // A control that cannot act must not still be armed. Tapping where the
  // headline was, with nothing to study, must do nothing at all.
  const auto* headline = out.target.find("DONE");
  CHECK(headline != nullptr);
  if (headline != nullptr) {
    const fui::ActionEvent event = out.tap(headline->rect.x + 20, headline->rect.y + 10);
    CHECK(event.action == fui::NO_ACTION);
  }
}

void testStudyForecastBarsStayInsideTheirPanel() {
  // Everything overdue piles onto today, so today's bar is an order of
  // magnitude taller than the rest. Scaling to it flattened the forecast to
  // one column and thirteen empty slots; today clips instead. Either way no
  // bar may escape the panel it was given.
  int forecast[studyui::kForecastDays] = {4000, 3, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Rendered out;
  buildStudyDeck(out, deckWithWork(forecast));

  const auto* caption = out.target.find("2 WEEKS BACK   TODAY   6 DUE AHEAD");
  CHECK(caption != nullptr);
  if (caption == nullptr) return;

  // Every fill must sit above the caption and below the header band: a bar
  // scaled off a 4000-card backlog would otherwise run up through the title.
  int bars = 0;
  for (const auto& rect : out.target.fills) {
    if (rect.width > 40) continue;  // rules and dividers, not bars
    ++bars;
    CHECK(rect.y >= toybox::kHeaderHeight);
    CHECK(rect.y + rect.height <= caption->rect.y);
  }
  // Today plus the three non-zero days ahead, each of which draws at least one
  // fill. If the scale ever silently drops a small day this count falls.
  CHECK(bars >= 4);
}

void testStudyRecordShowsTheStreak() {
  int forecast[studyui::kForecastDays] = {};
  int history[studyui::kHistoryDays] = {40, 22, 31};

  studyui::DeckModel model = deckWithWork(forecast);
  model.history = history;
  model.streak = 3;
  model.retention = 90;
  model.lifetimeReviews = 1204;
  Rendered out;
  buildStudyDeck(out, model);

  // The Record band is what you have done, which is the half of "stats" that
  // belongs on the front door rather than behind another tap.
  CHECK(out.target.drew("STREAK 3   90% RECALL   1204 REVIEWS"));

  // With no history at all it falls back to naming the deck rather than
  // printing a row of zeroes, which would read as a broken counter.
  Rendered fresh;
  studyui::DeckModel blank = deckWithWork(forecast);
  buildStudyDeck(fresh, blank);
  CHECK(fresh.target.drew("Mandarin: Vocabulary   5001 CARDS"));
  CHECK(!fresh.target.drew("STREAK 0   -1% RECALL   0 REVIEWS"));
}

void testStudyPanelSaysSoWhenItHasNothing() {
  // A fresh install with a backlog has no history and nothing scheduled ahead,
  // so every column is zero. An empty bracketed box reads as a panel that
  // failed to draw; this is the first thing a new deck shows.
  int forecast[studyui::kForecastDays] = {};
  forecast[0] = 289;  // all overdue, which lands on today and is not a column
  int history[studyui::kHistoryDays] = {};

  studyui::DeckModel model = deckWithWork(forecast);
  model.history = history;
  Rendered out;
  buildStudyDeck(out, model);
  CHECK(out.target.drew("NOTHING RECORDED YET"));

  // One day of history is enough to stop saying it.
  history[3] = 12;
  Rendered some;
  buildStudyDeck(some, model);
  CHECK(!some.target.drew("NOTHING RECORDED YET"));
}

void testStudyWarnsWhenAReviewDidNotSave() {
  int forecast[studyui::kForecastDays] = {};
  studyui::DeckModel model = deckWithWork(forecast);
  model.writeFailed = true;

  Rendered out;
  buildStudyDeck(out, model);
  // The one failure this app must never swallow.
  CHECK(out.target.drew("SOME REVIEWS DID NOT SAVE"));

  Rendered quiet;
  studyui::DeckModel ok = deckWithWork(forecast);
  buildStudyDeck(quiet, ok);
  CHECK(!quiet.target.drew("SOME REVIEWS DID NOT SAVE"));
}

}  // namespace

// --- vertical centring -------------------------------------------------------

// Every cut the fork ships, so a regenerated face is checked here as well as by
// verifyCutMetrics() on the device.
const toybox::CutMetrics kEveryCut[] = {toybox::kTileCut,
                                        toybox::kButtonCut,
                                        toybox::kUiCut,
                                        toybox::kDisplayCut,
                                        toybox::kReadingSmallCut,
                                        toybox::kReadingCut,
                                        toybox::kReadingBoldSmallCut,
                                        toybox::kReadingBoldCut};

void testInkCentredPutsTheInkInTheMiddleOfAnyBox() {
  for (const toybox::CutMetrics& cut : kEveryCut) {
    // From well under the line box to well over it. Under is where the target's
    // clamp bites; over is where it already worked, and must keep working.
    for (int16_t height = cut.inkHeight; height <= 100; ++height) {
      const fui::Rect box = fui::makeRect(40, 120, 200, height);
      const fui::Rect given = toybox::inkCentred(box, cut);
      const int above = inkTopIn(given, cut) - box.y;
      const int below = box.y + box.height - (inkTopIn(given, cut) + cut.inkHeight);
      // Centred means the two gaps match, to the pixel a whole-pixel offset can
      // manage. Stated as a symmetry rather than as a formula, so the check
      // cannot pass by restating the code it is checking.
      CHECK(above >= 0 && below >= 0);
      CHECK(above - below <= 1 && below - above <= 1);
      // The x axis is the caller's business and must survive untouched.
      CHECK(given.x == box.x);
      CHECK(given.width == box.width);
    }
  }
}

// The other half of the same claim: handing the target the box itself is wrong
// once the box is shorter than the line box, and wrong by more the smaller it
// gets. Without this the check above could pass against a target that never
// needed correcting.
void testAShortBoxIsWhatMakesTheCorrectionNecessary() {
  const toybox::CutMetrics& cut = toybox::kDisplayCut;
  // A 50px box under a 63px line box: the clamp pins the offset at zero, so the
  // ink lands `ascender - inkHeight` down and its foot leaves the box. That is
  // exactly what a Knucklebones total used to do.
  const fui::Rect tight = fui::makeRect(0, 0, 100, 50);
  CHECK(inkTopIn(tight, cut) == cut.ascender - cut.inkHeight);
  CHECK(inkTopIn(tight, cut) + cut.inkHeight > tight.height);
  CHECK(inkTopIn(toybox::inkCentred(tight, cut), cut) + cut.inkHeight <= tight.height);
  // And the error grows as the box shrinks, which is why it reads as an
  // intermittent font problem rather than as a rule.
  const fui::Rect tighter = fui::makeRect(0, 0, 100, 44);
  const int offBy = [&](const fui::Rect& box) { return inkTopIn(box, cut) - (box.height - cut.inkHeight) / 2; }(tight);
  const int offByMore = [&](const fui::Rect& box) {
    return inkTopIn(box, cut) - (box.height - cut.inkHeight) / 2;
  }(tighter);
  CHECK(offByMore > offBy);
}

// --- keeping a wrap between paints ---------------------------------------
//
// toybox::WrappedText keeps one wrap across paints. These tests turn a wrap
// kept after the thing it describes has moved into an assertion.

// A magazine feature's worth of prose, built rather than pasted so the cost
// can be asked at more than one length.
//
// Deliberately NON-REPEATING. A corpus built by rotating a handful of
// sentences wraps periodically, and a wrap read from the WRONG offset then
// lands on a line identical to the right one -- so a staleness test compares
// two different answers, gets the same text back, and passes because it cannot
// tell them apart. That happened here: the kerning test below was green
// against a repeating corpus before this was changed.
std::string longArticle(const size_t bytes) {
  static const char* kWords[] = {
      "the",  "panel", "is",    "a",     "page",   "of",          "text", "and",  "reader", "holds",  "it",     "still",
      "wrap", "walks", "every", "byte",  "asking", "font",        "how",  "wide", "each",   "prefix", "paying", "once",
      "cost", "twice", "bug",   "three", "none",   "constraints", "said", "walk", "had",    "happen"};
  std::string doc;
  uint32_t seed = 12345u;
  size_t i = 0;
  char stamp[24];
  while (doc.size() < bytes) {
    // Every stretch carries its own number, then a run of words of
    // unpredictable length, so no two parts of the document wrap alike.
    std::snprintf(stamp, sizeof(stamp), "[%zu]", i);
    doc += stamp;
    seed = seed * 1103515245u + 12345u;
    const int run = 4 + static_cast<int>((seed >> 16) % 11);
    for (int w = 0; w < run; ++w) {
      seed = seed * 1103515245u + 12345u;
      doc += ' ';
      doc += kWords[(seed >> 16) % 34];
    }
    doc += ' ';
    if (++i % 6 == 0) doc += '\n';
  }
  return doc;
}

// What fui::textArea() would have put on the panel, for the same rect, text,
// style and topLine. The wrap is only allowed to be faster; a single line of
// difference here is a page turn that skips or repeats a line, and Instapaper
// computes the reading position it sends to a real account from exactly this.
std::vector<std::string> linesFromTextArea(FakeTarget& target, const fui::Rect rect, const char* text,
                                           const fui::TextStyle& style, const uint32_t topLine) {
  toybox::Interactions interactions;
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, device(), noInput, interactions);
  fui::TextAreaProps props;
  props.text = text;
  props.topLine = topLine;
  props.showCaret = false;
  props.style = style;
  target.texts.clear();
  fui::textArea(frame, rect, props);
  std::vector<std::string> out;
  for (const auto& run : target.texts) {
    char where[48];
    std::snprintf(where, sizeof(where), "%d,%d|", static_cast<int>(run.rect.x), static_cast<int>(run.rect.y));
    out.push_back(std::string(where) + run.text);
  }
  return out;
}

std::vector<std::string> linesFromWrap(FakeTarget& target, toybox::WrappedText& wrap, const fui::Rect rect,
                                       const char* text, const fui::TextStyle& style, const uint32_t topLine) {
  target.texts.clear();
  wrap.draw(target, rect, text, style, topLine);
  std::vector<std::string> out;
  for (const auto& run : target.texts) {
    char where[48];
    std::snprintf(where, sizeof(where), "%d,%d|", static_cast<int>(run.rect.x), static_cast<int>(run.rect.y));
    out.push_back(std::string(where) + run.text);
  }
  return out;
}

// A rotation is a different width, and a width is the whole wrap.
void testANarrowerPanelIsNotDrawnFromTheWiderPanelsWrap() {
  const std::string doc = longArticle(16u * 1024u);
  const fui::TextStyle style = toybox::themeTokens().bodyText;
  FakeTarget target;
  toybox::WrappedText wrap;

  const uint32_t wide = wrap.lineCount(target, 440, doc.c_str(), style);
  const uint32_t narrow = wrap.lineCount(target, 260, doc.c_str(), style);
  CHECK(wrap.wraps() == 2);
  CHECK(narrow > wide);
  // Not just a different number: the narrow panel's own answer.
  toybox::WrappedText fresh;
  FakeTarget clean;
  CHECK(narrow == fresh.lineCount(clean, 260, doc.c_str(), style));
  // And back again, so this is not one-way.
  CHECK(wrap.lineCount(target, 440, doc.c_str(), style) == wide);
}

// The one the previous version of this file could not ask.
//
// The old layer 2 compared how many lines the window produced against how many
// the index recorded. A metrics change that moves where the breaks fall while
// leaving the COUNT alone therefore slipped through it completely: the window
// was cut at a byte the stale index chose and re-wrapped with the new metrics,
// so the page began in the wrong place and the text between two pages was
// simply never shown.
//
// The adversarial case is SEARCHED FOR rather than assumed. A hand-picked
// kern delta that happens to change the line count tests the thing layer 2
// already measured, which is how this survived: the test was derived from the
// code's own assumption. Here the test hunts for a delta that preserves the
// count and changes the words, and says so out loud if it cannot find one.
// A document of even paragraphs, each its own block of lines, so that widening
// one and narrowing another are independent events whose effects on a line
// COUNT can cancel exactly.
std::string paragraphedArticle() {
  std::string doc;
  char stamp[32];
  for (int p = 0; p < 400; ++p) {
    std::snprintf(stamp, sizeof(stamp), "[%d] ", p);
    doc += stamp;
    for (int w = 0; w < 14; ++w) doc += "wordy ";
    doc += '\n';
  }
  return doc;
}

// The same shape in the other reader. Hacker News flattens a whole comment
// thread into one buffer and pages through it, and it had the identical two
// walks with not even a branch to hang a cache on.
void testTheHackerNewsReaderAlsoWrapsOncePerDocument() {
  const std::string doc = longArticle(32u * 1024u);
  const fui::TextStyle style = toybox::themeTokens().bodyText;
  const fui::Rect body = hnui::readerBody(device());
  FakeTarget target;
  toybox::WrappedText wrap;

  hnui::ReaderBody counted;
  counted.text = doc.c_str();
  counted.style = style;
  counted.wrap = &wrap;
  const uint32_t total = hnui::readerLineCount(target, device(), counted);
  CHECK(total > 400);
  CHECK(wrap.wraps() == 1);
  const uint16_t visible = fui::textAreaVisibleLines(body, target.lineHeight(style.font));
  FakeTarget slow;
  for (uint32_t top = 0; top < total; top += visible) {
    CHECK(linesFromWrap(target, wrap, body, doc.c_str(), style, top) ==
          linesFromTextArea(slow, body, doc.c_str(), style, top));
  }
  CHECK(wrap.wraps() == 1);
}

void testFitLinesCutsAnUnbreakableTokenRatherThanVanishing() {
  Rendered out;
  const fui::TextStyle style = toybox::themeTokens().bodyText;
  const std::string fitted = toybox::fitLines(out.target, "mario@averylongdomainnameindeed.example.com", 80, 1, style);
  CHECK(fitted.size() > 3);
  CHECK(fitted.rfind("...") == fitted.size() - 3);
  CHECK(fitted.compare(0, 5, "mario") == 0);
  // And it still fits, which is the whole point of cutting it.
  CHECK(out.target.measureText(style.font, fitted.c_str(), style).width <= 80);

  // A box too narrow for even one character plus the mark gives back nothing
  // rather than a bare ellipsis, so a caller drawing it shows an empty row
  // instead of a row that looks like it lost its content.
  CHECK(toybox::fitLines(out.target, "mario@example.com", 4, 1, style).empty());

  // The ordinary case is untouched: a sentence wide enough for several words
  // still breaks between them and never mid-word. Width chosen to hold more
  // than one word, or this would assert nothing.
  const std::string sentence = toybox::fitLines(out.target, "one two three four five six seven", 240, 1, style);
  CHECK(sentence.find(' ') != std::string::npos);
  CHECK(sentence.find("...") != std::string::npos);
  // The cut lands on a boundary: the character before the mark is not a
  // fragment of a word that continues.
  const std::string kept = sentence.substr(0, sentence.size() - 3);
  CHECK(std::string("one two three four five six seven").compare(0, kept.size(), kept) == 0);
}

// --- the header band under the bezel ---------------------------------------
//
// Every other test in this file builds against device(), whose safeArea is
// empty -- which pins the same geometry with and without the glass BY
// CONSTRUCTION, and is exactly why nothing here could see this. The X4 Pro's
// bezel covers the panel's top ten rows and one column each side
// (docs/bezel-insets.md), and a band that starts painting below them leaves
// paper where the eye expects ink. Head-on that strip is under the glass and
// invisible; from below, the glass sits above the panel and the eye sees past
// its edge, so a white line appears over every black header in the fork. Mario
// reported it off-axis on APPS and GAMES; it was on every toybox band in the
// fork -- 41 call sites across 27 files.

// The X4 Pro's frame WITH its measured insets. Only a context that has them can
// fail the checks below.
fui::DeviceContext bezelDevice() {
  fui::DeviceContext ctx = device();
  ctx.safeArea = fui::Insets{10, 1, 0, 1};
  return ctx;
}

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void renderWithBezel(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, bezelDevice(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

// The other half of the split: paint bleeds under the glass, ink does not. A
// band filled to row 0 by a fix that also moved the title up there would pass
// the check above and be a worse bug than the one it closed.
void testTheHeaderTitleStaysOutOfTheCoveredRows() {
  fui::ListItem items[1] = {};
  items[0].label = "SOLITAIRE";
  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.items = items;
  model.count = 1;

  Rendered out;
  renderWithBezel<shelfui::MenuModel, shelfui::buildMenu>(out, model);
  const FakeTarget::TextRun* title = out.target.find("APPS & GAMES");
  CHECK(title != nullptr);
  if (title != nullptr) {
    // Centred in the VISIBLE part, not in the whole band: equal air above and
    // below, measured from the bezel's safe top rather than from row 0. Filling
    // the band to row 0 and centring the title over all of it passes every
    // coverage check above and drops the title's air into rows nobody can see,
    // which is the bug this fix could easily have introduced.
    const fui::Rect ink = inkBandOf(*title);
    const int above = ink.y - bezelDevice().safeArea.top;
    const int below = toybox::kHeaderHeight - ink.bottom();
    CHECK(above > 0);
    CHECK(above - below <= 1 && below - above <= 1);
  }
}

// And the third: the band's BOTTOM edge is what every layout below it is tuned
// against, so widening the paint upward must not move it. Under absolute chrome
// that edge is kHeaderHeight, with or without the glass.
// And the band is absolute WITHOUT the screen asking, which is the half that
// was missing. absoluteChrome() used to be an opt-in call placed before
// headerBand(), and screens forgot it the same way they forgot the rule:
// Yahtzee called it on its menu and not on its card, so the card's band began
// at the bezel's safe top and painted 85 rows where the menu painted 76. Two
// headers, two heights, in one game. This drives headerBand() directly on a
// bezelled frame with no absoluteChrome() call of its own, which is exactly
// what those screens did.
void testTheBandIsAbsoluteWithoutBeingAsked() {
  Rendered out;
  const fui::DeviceContext ctx = bezelDevice();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  fui::HeaderProps props;
  props.title = "TITLE";
  toybox::headerBand(screen, props);

  // The bezel must not push the band down. Both numbers matter: a band that
  // starts at the safe top AND keeps its height ends kHeaderHeight + 10 down
  // the panel, which is the 85px band Mario saw. Measured at the body's top,
  // which is the band plus the rule under it -- the whole chrome, because the
  // whole chrome is what a screen has to clear.
  CHECK(screen.body().y == toybox::kChromeHeight);

  bool paintedFromRowZero = false;
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Rect& r = out.target.fills[i];
    if (r.y == 0 && r.height == toybox::kHeaderHeight && r.width == ctx.screen().width) paintedFromRowZero = true;
  }
  CHECK(paintedFromRowZero);

  // And the rule tracks it, rather than sitting 10px lower on this screen than
  // on its sibling.
  bool ruled = false;
  for (size_t i = 0; i < out.target.fills.size(); ++i) {
    const fui::Rect& r = out.target.fills[i];
    if (r.y == toybox::kHeaderHeight + toybox::kBandRuleGap && r.height == toybox::kRule) ruled = true;
  }
  CHECK(ruled);
}

void testTheHeaderBandBottomIgnoresTheBezel() {
  fui::ListItem items[1] = {};
  items[0].label = "SOLITAIRE";
  shelfui::MenuModel model;
  model.title = "APPS & GAMES";
  model.items = items;
  model.count = 1;

  Rendered bare;
  {
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(bare.target, device(), noInput, bare.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    shelfui::buildMenu(screen, model);
  }
  Rendered glassed;
  renderWithBezel<shelfui::MenuModel, shelfui::buildMenu>(glassed, model);

  const FakeTarget::TextRun* bare0 = bare.target.find("SOLITAIRE");
  const FakeTarget::TextRun* glassed0 = glassed.target.find("SOLITAIRE");
  CHECK(bare0 != nullptr);
  CHECK(glassed0 != nullptr);
  if (bare0 != nullptr && glassed0 != nullptr) {
    CHECK(bare0->rect.y == glassed0->rect.y);
  }
}

// --- the BODY under the bezel -----------------------------------------------
//
// The band absorbs the glass. The body must not absorb it a second time.
//
// toybox::kHeaderHeight, kChromeHeight and kBodyTop are ABSOLUTE panel rows:
// headerBand() calls absoluteChrome() before it takes the band, so the band
// paints from row 0 and its bottom edge lands at kHeaderHeight whatever the
// bezel hides -- testTheBandIsAbsoluteWithoutBeingAsked above pins exactly
// that. The ten covered rows are therefore already inside the band's paint,
// and a screen that adds safeArea.top to a chrome-derived top pushes its body
// ten pixels below every other app's and buys nothing.
//
// xkcd and Wallpapers did, for as long as both apps had existed, and the
// comment above each constant claimed it lined up with the shelf. NOTHING
// here could see it: every other test in this file builds against device(),
// whose safeArea is empty, and twice nothing is nothing. That absent coverage
// is the defect card 358 was really about, so the checks come in two parts:
// the alignment (all four apps on one row) and the rule that keeps it (the
// glass may not move a body top), the second of which a screen written
// tomorrow cannot pass by accident.

// Every row a screen's own content occupies: the y of everything drawn at or
// below kChromeHeight, which is where headerBand()'s ownership ends.
//
// The WHOLE list, sorted, not just the topmost. Comparing only the first row
// would pass a screen whose first element is absolute and whose later ones add
// safe.y -- and half a screen compensating is exactly the shape this fork keeps
// shipping (see the two-input-paths notes). Sorted rather than positional
// because draw order is not layout order.
//
// Rects rather than ink bands on purpose. toybox::inkCentred() expands a text
// rect around its cut, so a recorded rect can start above the band it was laid
// into -- xkcd's menu headline draws at y=102 for a band at 112. That expansion
// is identical in both contexts, so it cancels in a comparison and would only
// mislead an absolute assertion. The absolute row is asserted from the exported
// geometry instead, in testEveryAppsBodyStartsOnTheSameRow.
std::vector<int> bodyRows(const FakeTarget& target) {
  std::vector<int> rows;
  for (const FakeTarget::TextRun& run : target.texts) {
    if (run.rect.y >= toybox::kChromeHeight) rows.push_back(run.rect.y);
  }
  for (const fui::Rect& r : target.fills) {
    if (r.y >= toybox::kChromeHeight) rows.push_back(r.y);
  }
  for (const FakeTarget::Stroke& st : target.strokes) {
    if (st.rect.y >= toybox::kChromeHeight) rows.push_back(st.rect.y);
  }
  for (const FakeTarget::Blit& b : target.blits) {
    if (b.rect.y >= toybox::kChromeHeight) rows.push_back(b.rect.y);
  }
  std::sort(rows.begin(), rows.end());
  return rows;
}

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void render(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

// The rule: rendered twice, once on a bare frame and once behind the glass,
// every body row lands in the same place. Nothing about the app's own gutter is
// assumed, which is what lets a screen nobody has written yet be added here in
// two lines.
//
// The emptiness check is not ceremony. Comparing two renders that drew NOTHING
// below the chrome compares two empty lists and passes, so a model that fails
// to produce a body -- a wrong fixture, a builder that early-returns -- would
// report this rule as satisfied. That is the failure mode a guard has when its
// subject is absent, and it is the one this suite has been bitten by before.
template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void checkTheGlassDoesNotMoveTheBody(const Model& model, const char* what) {
  Rendered bare;
  render<Model, Build>(bare, model);
  Rendered glassed;
  renderWithBezel<Model, Build>(glassed, model);
  const std::vector<int> bareRows = bodyRows(bare.target);
  const std::vector<int> glassedRows = bodyRows(glassed.target);
  check(!bareRows.empty(), what, __LINE__);
  check(bareRows == glassedRows, what, __LINE__);
}

void testTheGlassNeverMovesABodyTop() {
  fui::ListItem rows[3] = {};
  rows[0].label = "First";
  rows[1].label = "Second";
  rows[2].label = "Third";

  {
    shelfui::MenuModel model;
    model.title = "APPS & GAMES";
    model.items = rows;
    model.count = 3;
    checkTheGlassDoesNotMoveTheBody<shelfui::MenuModel, shelfui::buildMenu>(
        model, "shelf folder: the glass does not move the body");
  }
  {
    hnui::ListModel model;
    model.items = rows;
    model.count = 3;
    checkTheGlassDoesNotMoveTheBody<hnui::ListModel, hnui::buildList>(
        model, "hacker news list: the glass does not move the body");
  }
  {
    xkcdui::ListModel model;
    model.items = rows;
    model.count = 3;
    checkTheGlassDoesNotMoveTheBody<xkcdui::ListModel, xkcdui::buildList>(
        model, "xkcd list: the glass does not move the body");
  }
  {
    // The front door, whose headline is the one run inkCentred() expands.
    xkcdui::MenuModel model;
    checkTheGlassDoesNotMoveTheBody<xkcdui::MenuModel, xkcdui::buildMenu>(
        model, "xkcd menu: the glass does not move the body");
  }
  {
    xkcdui::NumberModel model;
    model.typed = "12";
    model.firstNum = 1;
    model.maxNum = 3281;
    checkTheGlassDoesNotMoveTheBody<xkcdui::NumberModel, xkcdui::buildNumber>(
        model, "xkcd number pad: the glass does not move the body");
  }
  {
    wallpapersui::GridChromeModel model;
    model.title = "WALLPAPERS";
    model.warning = "Card is nearly full";
    checkTheGlassDoesNotMoveTheBody<wallpapersui::GridChromeModel, wallpapersui::buildGridChrome>(
        model, "wallpapers grid: the glass does not move the body");
  }
  // Card 365's two screens, added to card 358's guard rather than left outside
  // it. The grid above was the only wallpapers screen listed, and these two
  // reach the panel by a different route (a hold, not a tap), so a clean run of
  // the list above said nothing at all about them -- which is exactly how the
  // ten-pixel drop survived in this app while twenty others were right.
  {
    wallpapersui::SheetModel model;
    model.name = "Holiday In Lisbon";
    checkTheGlassDoesNotMoveTheBody<wallpapersui::SheetModel, wallpapersui::buildSheet>(
        model, "wallpapers hold sheet: the glass does not move the body");
  }
  {
    wallpapersui::ConfirmModel model;
    model.name = "Holiday In Lisbon";
    model.consequence = "Your own wallpaper. The card holds the only copy, so this cannot be undone.";
    checkTheGlassDoesNotMoveTheBody<wallpapersui::ConfirmModel, wallpapersui::buildConfirm>(
        model, "wallpapers delete confirm: the glass does not move the body");
  }
}

// Moving a body top moves everything under it, and this fork has already
// shipped a box nudged to satisfy one rule that landed on its neighbour. So:
// what did card 358 land on?
//
// Wallpapers is the screen that pays. Its grid is height-constrained between
// the hint strip and the page dots, so the fourteen pixels the fix gave back
// to the top come out of the THUMBNAILS, not off the bottom -- gridGeom()
// re-fits the cells into whatever height is left, which is also why a
// collision assertion here would be untestable: the cells shrink toward 1px
// rather than ever overlapping the dots. Verified by inflating kBodyTop by
// 300 and watching six other suites go red while a collision check stayed
// green.
//
// A floor on the cell is therefore the assertion that can actually fail. The
// measured size behind the glass is 150x249 after the fix, down from 154x256 --
// BOTH axes, because the cell is height-bound here and the width follows the
// aspect, so the thumbnails lost about 5.2% of their area.
//
// WHAT THIS DOES NOT CATCH, so nobody reads it as more than it is: the floor
// trips at roughly +28px of body top on height and +44px on width, so it would
// stay GREEN if this very bug were reintroduced -- a ten-pixel push leaves the
// cell at 145x241, comfortably inside it. It is a gross-degradation guard, not
// a guard on this card's defect; testTheGlassNeverMovesABodyTop and
// testEveryAppsBodyStartsOnTheSameRow are what catch that, exactly. Set from
// the measured size minus a little slack rather than tight against it, because
// a floor that trips on any legitimate re-tuning gets deleted rather than
// heeded.
void testTheWallpapersThumbnailsStayBigEnoughToRead() {
  for (const fui::DeviceContext& ctx : {device(), bezelDevice()}) {
    const wallpapersui::GridGeom g = wallpapersui::gridGeom(ctx);
    CHECK(g.cellW >= 140);
    CHECK(g.cellH >= 240);
    // And the first row still starts under the hint strip rather than in it.
    CHECK(wallpapersui::cellRect(g, 0).y >= toybox::kBodyTop + 30);
    // The bottom seam, for completeness: the last caption is above the dots.
    CHECK(wallpapersui::captionRect(g, g.perPage - 1).bottom() <= g.pageDotsY);
  }
}

// The two paths to a body top, pinned to each other.
//
// A screen holding a Screen& gets its body from headerBand()'s reservation
// plus insetContent(); a geometry function an Activity shares with its builder
// has no Screen and reaches for toybox::kBodyTop instead. Those are two
// expressions of one fact, and card 248 is the proof they drift: it moved the
// reservation from the band alone to band + gap + rule, and any kBodyTop
// written as its own sum of kHeaderHeight and three gutters would have kept the
// old number while every component-laid screen moved seven pixels down. Nothing
// would have gone red -- both paths are internally consistent, they just stop
// agreeing with each other.
//
// So kBodyTop is DERIVED (bodyTopBelow -> chromeBelow), and this asserts the
// derivation against what the component path actually produces. Change either
// side alone and this is what fails.
void testTheHandRolledBodyTopMatchesTheReservedOne() {
  for (const fui::DeviceContext& ctx : {device(), bezelDevice()}) {
    Rendered out;
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    fui::HeaderProps props;
    props.title = "TITLE";
    toybox::headerBand(screen, props);
    screen.insetContent(fui::Insets{toybox::kBodyGutter, toybox::kMargin, toybox::kMargin, toybox::kMargin});
    CHECK(screen.body().y == toybox::kBodyTop);
  }
  // And the derivation is the reservation's, not a second sum that happens to
  // agree today: kBodyTop must track a band height it was not written against.
  // 56 is Solitaire's landscape band (solitaireui::kHeaderBand), the one real
  // case where kHeaderHeight is not the band height.
  CHECK(toybox::bodyTopBelow(toybox::kHeaderHeight) == toybox::kBodyTop);
  CHECK(toybox::bodyTopBelow(56) == toybox::chromeBelow(56) + toybox::kBodyGutter);
  CHECK(toybox::bodyTopBelow(56) != toybox::kBodyTop);

  // A REAL screen, not just the synthetic chrome above. The assertions so far
  // drive headerBand() directly; this drives one of the ~41 app screens that
  // reach insetContent({kBodyGutter, ...}) through their own chrome() helper,
  // and reads where its first component-laid element actually landed.
  //
  // Without this the suite measures screen.body().y in exactly two places, both
  // against kHeaderHeight, and no app screen's component-laid body top is
  // measured anywhere -- so the two halves of the fork's layout could disagree
  // with nothing to say so. takeTop() returns a rect at content_.y and consumes
  // the gap AFTER it, so the first one is the body top exactly, and this run is
  // drawn from a raw rect with no inkCentred() expansion.
  for (const fui::DeviceContext& ctx : {device(), bezelDevice()}) {
    wallpapersui::EmptyModel empty;
    empty.title = "WALLPAPERS";
    empty.warning = nullptr;
    Rendered out;
    const fui::InputSnapshot noInput{};
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wallpapersui::buildEmpty(screen, empty);
    const FakeTarget::TextRun* headline = out.target.find("NO WALLPAPERS");
    CHECK(headline != nullptr);
    if (headline != nullptr) CHECK(headline->rect.y == toybox::kBodyTop);
  }
}

// And the alignment itself, from the geometry the Activities share rather than
// from a render, so the number is the one the paging arithmetic uses too.
// Asserted BEHIND THE GLASS: on a bare frame these agreed all along, which is
// the whole reason the misalignment shipped.
void testEveryAppsBodyStartsOnTheSameRow() {
  const fui::DeviceContext glass = bezelDevice();
  CHECK(shelfui::listBand(glass, true, false).y == toybox::kBodyTop);
  CHECK(hnui::listBand(glass).y == toybox::kBodyTop);
  CHECK(xkcdui::listBand(glass).y == toybox::kBodyTop);

  // Wallpapers is deliberately NOT in the list above, and the reason is worth
  // stating because this test used to assert it was.
  //
  // Its hint strip was standing in for a body top this screen does not export.
  // That strip is CHROME -- one line about the grid, the twin of a subtitle --
  // and its body is the grid itself, which hangs a fixed distance lower and
  // never lined up with anybody's first row anyway. Pinning the strip to
  // kBodyTop spent the whole body gutter above the sentence and left a sixth
  // of it below, so it read as a caption stuck to the tiles. Mario reported
  // that three times.
  //
  // What actually has to hold is below: the strip is centred between the rule
  // and the grid, and the GRID has not moved, because on this screen every row
  // given to the top comes out of the thumbnails
  // (testTheWallpapersThumbnailsStayBigEnoughToRead).
  wallpapersui::GridChromeModel model;
  model.title = "WALLPAPERS";
  model.warning = "Card is nearly full";
  Rendered out;
  renderWithBezel<wallpapersui::GridChromeModel, wallpapersui::buildGridChrome>(out, model);
  const FakeTarget::TextRun* hint = out.target.find("Card is nearly full");
  CHECK(hint != nullptr);
  if (hint != nullptr) {
    const wallpapersui::GridGeom g = wallpapersui::gridGeom(bezelDevice());
    const int16_t ruleBottom = toybox::kChromeHeight;
    const int16_t above = static_cast<int16_t>(hint->rect.y - ruleBottom);
    const int16_t below = static_cast<int16_t>(g.originY - hint->rect.bottom());
    // Within the strip's own slack: the box is centred, and where the ink sits
    // inside it belongs to the cut's line box, not to this layout.
    const int16_t skew = static_cast<int16_t>(above > below ? above - below : below - above);
    CHECK(skew <= 4);
    // And the strip sits BELOW the rule with room, never under the band.
    CHECK(above > 0);
  }
}

bool allDigits(const std::string& s) {
  if (s.empty()) return false;
  for (const char ch : s)
    if (ch < '0' || ch > '9') return false;
  return true;
}

// --- Wallpapers -------------------------------------------------------------

void buildWallpapersChrome(Rendered& out, const wallpapersui::GridChromeModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  wallpapersui::buildGridChrome(screen, model);
}

void buildWallpapersEmpty(Rendered& out, const wallpapersui::EmptyModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  wallpapersui::buildEmpty(screen, model);
}

// Two columns, always -- the point of the grid -- and slot 1 is to the right of
// slot 0 on the same row; slot 2 drops to the next row's first column.
void testWallpapersGridHasTwoColumns() {
  const wallpapersui::GridGeom g = wallpapersui::gridGeom(device());
  CHECK(g.cols == 2);
  CHECK(g.perPage >= 2);
  const fui::Rect c0 = wallpapersui::cellRect(g, 0);
  const fui::Rect c1 = wallpapersui::cellRect(g, 1);
  CHECK(c1.x > c0.x);
  CHECK(c1.y == c0.y);
  if (g.perPage >= 3) {
    const fui::Rect c2 = wallpapersui::cellRect(g, 2);
    CHECK(c2.y > c0.y);
    CHECK(c2.x == c0.x);
  }
}

// Every cell sits inside the panel: a thumbnail drawn off-screen is one nobody
// sees.
void testWallpapersCellsStayOnScreen() {
  const fui::DeviceContext dev = device();
  const wallpapersui::GridGeom g = wallpapersui::gridGeom(dev);
  for (int slot = 0; slot < g.perPage; ++slot) {
    const fui::Rect c = wallpapersui::cellRect(g, slot);
    CHECK(c.x >= 0);
    CHECK(c.y >= 0);
    CHECK(c.right() <= dev.width);
    CHECK(c.bottom() <= dev.height);
  }
}

// The tap hit-test reads the SAME rectangles the Activity draws into: the centre
// of each cell routes to that cell, and a point up in the header routes to none.
void testWallpapersCellHitTestMatchesDraw() {
  const wallpapersui::GridGeom g = wallpapersui::gridGeom(device());
  for (int slot = 0; slot < g.perPage; ++slot) {
    const fui::Rect c = wallpapersui::cellRect(g, slot);
    CHECK(wallpapersui::cellAt(g, c.x + c.width / 2, c.y + c.height / 2) == slot);
  }
  CHECK(wallpapersui::cellAt(g, 0, 0) == -1);
  CHECK(wallpapersui::cellAt(g, -5, -5) == -1);
}

// A grid with nothing set must SAY to tap one, or it reads as a selection that
// failed to draw (a-silent-screen-reads-as-a-crash).
void testWallpapersChromeSaysTapToSetWhenNothingIsSet() {
  Rendered out;
  wallpapersui::GridChromeModel model;
  model.rightLabel = "6 SAVED";
  model.hasActive = false;
  buildWallpapersChrome(out, model);
  CHECK(drewText(out, "WALLPAPERS"));
  CHECK(drewText(out, "6 SAVED"));
  CHECK(drewText(out, "Tap one to set"));
}

// With one set, the hint is gone -- the thick border the Activity draws is the
// indicator.
void testWallpapersChromeIsQuietWhenSomethingIsSet() {
  Rendered out;
  wallpapersui::GridChromeModel model;
  model.rightLabel = "6 SAVED";
  model.hasActive = true;
  buildWallpapersChrome(out, model);
  CHECK(!drewText(out, "Tap a wallpaper"));
}

// The page label is shown verbatim so a paged library says where you are.
void testWallpapersChromeShowsThePage() {
  Rendered out;
  wallpapersui::GridChromeModel model;
  model.rightLabel = "PAGE 2 / 3";
  model.hasActive = true;
  buildWallpapersChrome(out, model);
  CHECK(drewText(out, "PAGE 2 / 3"));
}

// LIVE IS SHOWING, and the strip has to say so. Without this the grid drew the
// selection marker on the "Your phone" tile while the strip said "Tap one to
// set your sleep screen." -- nothing is set, beside a mark saying something is.
// That is card #354's shape: the marker and the words disagreeing, with nothing
// on the screen to say why.
void testWallpapersChromeSaysWhenLiveIsTheSleepScreen() {
  Rendered out;
  wallpapersui::GridChromeModel model;
  model.rightLabel = "6 SAVED";
  // hasActive stays false on purpose: Live and a pinned wallpaper are mutually
  // exclusive, so this is exactly the state the old strip got wrong.
  model.hasActive = false;
  model.liveOn = true;
  buildWallpapersChrome(out, model);
  CHECK(drewText(out, wallpapersui::liveStripLine()));
  CHECK(!drewText(out, "Tap one to set"));
}

// ...but it does not silence the two lines above it. Both are NEWS -- something
// changed behind the user's back, or the card is filling -- and Live being on
// is a standing state that would suppress either for the whole session. That
// suppression is #354 itself, so the order is asserted rather than assumed.
void testWallpapersChromeLiveDoesNotDisplaceTheNoteOrTheWarning() {
  {
    Rendered out;
    wallpapersui::GridChromeModel model;
    model.hasActive = false;
    model.liveOn = true;
    model.note = "Sleep screen was off. It is on now.";
    buildWallpapersChrome(out, model);
    CHECK(drewText(out, "Sleep screen was off."));
    CHECK(!drewText(out, wallpapersui::liveStripLine()));
  }
  {
    Rendered out;
    wallpapersui::GridChromeModel model;
    model.hasActive = false;
    model.liveOn = true;
    model.warning = "Could not check card space.";
    buildWallpapersChrome(out, model);
    CHECK(drewText(out, "Could not check card space."));
    CHECK(!drewText(out, wallpapersui::liveStripLine()));
  }
}

// The free-space advisory wins the hint strip and is shown verbatim: "full" and
// "could not tell" are different sentences.
void testWallpapersChromeWarningVerbatim() {
  Rendered out;
  wallpapersui::GridChromeModel model;
  model.rightLabel = "1 SAVED";
  model.hasActive = false;
  model.warning = "Could not check card space.";
  buildWallpapersChrome(out, model);
  CHECK(drewText(out, "Could not check card space."));
  CHECK(!drewText(out, "Tap a wallpaper"));
}

// The selection marker lives in the padding, and the caption's line box is
// reserved for EVERY cell whether or not it is selected. A cell whose contents
// move when it becomes selected is the same defect class as a marker that reads
// as image content: selecting should ADD A MARK, never re-flow the cell.
void testWallpapersCaptionNeverCollidesWithArtwork() {
  const wallpapersui::GridGeom g = wallpapersui::gridGeom(device());
  // The marker is drawn kMarkerGap (5) outside the thumbnail and is
  // kMarkerWeight (4) thick, so it reaches 9px below the artwork.
  const int markerReach = 5 + 4;
  CHECK(g.markerRoom > markerReach);  // clearance, not a collision
  for (int slot = 0; slot < g.perPage; ++slot) {
    const fui::Rect th = wallpapersui::thumbRect(g, slot);
    const fui::Rect cap = wallpapersui::captionRect(g, slot);
    // The caption starts below the artwork AND below the marker's reach.
    CHECK(cap.y >= th.bottom() + g.markerRoom);
    CHECK(cap.y > th.bottom() + markerReach);
    // It is inside the cell, so a caption cannot spill onto the row below.
    const fui::Rect cell = wallpapersui::cellRect(g, slot);
    CHECK(cap.bottom() <= cell.bottom());
    CHECK(cap.y >= cell.y);
  }
}

// The empty state names the gap and how to fix it -- and no longer sends anyone
// to a computer. It named File Transfer until the phone flow existed.
void testWallpapersEmptyStateSaysSomething() {
  Rendered out;
  wallpapersui::EmptyModel model;
  buildWallpapersEmpty(out, model);
  CHECK(drewText(out, "NO WALLPAPERS"));
  CHECK(drewText(out, "+ Add a wallpaper"));
  CHECK(!drewText(out, "File Transfer"));
}

// The address the QR encodes is NOT the one printed large, and the printed
// second line disappears when there is nothing true to put in it.
//
// This pins the fix for a fault the code could already see: startAddServer()
// logged a failed MDNS.begin() and then encoded the .local name anyway, so the
// phone said "cannot find server" while the prose blamed the user's WiFi. The
// activity now hands an EMPTY altUrl in that case, and this asserts the screen
// draws nothing rather than an address that cannot resolve.
void testWallpapersAddScreenDropsAnAddressItCannotStandBehind() {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  {
    Rendered out;
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wallpapersui::AddModel model;
    model.url = "http://crossplay-a1b2c3.local/w";
    model.altUrl = "http://192.168.1.42/w";
    const wallpapersui::AddRects rects = wallpapersui::buildAdd(screen, model);
    CHECK(drewText(out, "SCAN THIS CODE"));
    CHECK(drewText(out, "crossplay-a1b2c3.local/w"));
    CHECK(drewText(out, "192.168.1.42/w"));
    // The scheme is encoded, never drawn: it costs the address its type cut.
    CHECK(!drewText(out, "http://crossplay-a1b2c3.local/w"));
    CHECK(rects.qr.width > 0 && rects.qr.height > 0);
    // Nothing has arrived, so there is no picture to place and the Activity is
    // told so rather than left to work it out from a name it does not have.
    CHECK(rects.thumb.width == 0 && rects.thumb.height == 0);
    // And once one has, the picture takes the square and the address goes with
    // the code: the whole screen changes, so neither rect may be left behind.
    {
      Rendered landed;
      toybox::Frame f2(landed.target, ctx, noInput, landed.interactions);
      toybox::Screen s2(f2, toybox::themeTokens());
      wallpapersui::AddModel arrived = model;
      arrived.arrived = "w0007";
      const wallpapersui::AddRects r2 = wallpapersui::buildAdd(s2, arrived);
      CHECK(r2.thumb.width == wallpapersui::addPictureSide());
      CHECK(r2.qr.width == 0 && r2.qr.height == 0);
      CHECK(drewText(landed, "w0007"));
      CHECK(!drewText(landed, "SCAN THIS CODE"));
      CHECK(!drewText(landed, "crossplay-a1b2c3.local/w"));
    }
  }
  {
    Rendered out;
    toybox::Frame frame(out.target, ctx, noInput, out.interactions);
    toybox::Screen screen(frame, toybox::themeTokens());
    wallpapersui::AddModel model;
    model.url = "http://192.168.1.42/w";  // mDNS did not start: the address takes the line
    model.altUrl = nullptr;
    wallpapersui::buildAdd(screen, model);
    CHECK(drewText(out, "192.168.1.42/w"));
    CHECK(!drewText(out, ".local"));
  }
}

// The Offer screen is the ONLY screen a factory device shows, so the phone flow
// has to be reachable from it. It was a sentence, and ActionAddOwn was routed
// but drawn by nothing at all -- so a new reader could not reach the feature
// this app is now built around.
void testWallpapersOfferReachesTheAddFlow() {
  Rendered out;
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  wallpapersui::OfferModel model;
  model.count = 21;
  model.bytes = 1009302;
  wallpapersui::buildOffer(screen, model);
  bool addOwn = false;
  for (size_t i = 0; i < out.interactions.count(); ++i)
    if (out.interactions.data()[i].action == wallpapersui::ActionAddOwn) addOwn = true;
  CHECK(addOwn);
  CHECK(!out.interactions.overflowed());
}

// --- Wallpapers: the hold sheet ---------------------------------------------

void buildWallpapersSheet(Rendered& out, const wallpapersui::SheetModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  wallpapersui::buildSheet(screen, model);
}

void buildWallpapersConfirm(Rendered& out, const wallpapersui::ConfirmModel& model) {
  const fui::DeviceContext ctx = device();
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  wallpapersui::buildConfirm(screen, model);
}

bool registered(const Rendered& out, const fui::ActionId action) {
  for (size_t i = 0; i < out.interactions.count(); ++i) {
    if (out.interactions.data()[i].action == action) return true;
  }
  return false;
}

// The sheet a hold opens: it names the wallpaper, offers exactly the two things
// a tap cannot do, and says how to get out of the preview BEFORE opening it --
// the preview draws no chrome at all, so this is the only place that can.
void testWallpapersSheetOffersPreviewAndDelete() {
  Rendered out;
  wallpapersui::SheetModel model;
  model.name = "Duerer: Four Horsemen";
  buildWallpapersSheet(out, model);
  CHECK(drewText(out, "WALLPAPER"));
  CHECK(drewText(out, "Duerer: Four Horsemen"));
  CHECK(drewText(out, "PREVIEW"));
  CHECK(drewText(out, "DELETE"));
  CHECK(drewText(out, "tap it to come back"));
  CHECK(registered(out, wallpapersui::ActionPreview));
  CHECK(registered(out, wallpapersui::ActionDelete));
  // The sheet is the SAFE screen: nothing on it deletes anything. That is what
  // makes reusing its DELETE pixels for the confirm's KEEP IT sound.
  CHECK(!registered(out, wallpapersui::ActionConfirmDelete));
}

// The sheet says so when the wallpaper it is about is the one in use, because
// the delete's consequence differs for it and the user should learn that before
// the confirm rather than in it.
void testWallpapersSheetSaysWhenItIsTheOneInUse() {
  Rendered active;
  wallpapersui::SheetModel model;
  model.name = "Bauhaus";
  model.isActive = true;
  buildWallpapersSheet(active, model);
  CHECK(drewText(active, "on your sleep screen now"));

  Rendered idle;
  model.isActive = false;
  buildWallpapersSheet(idle, model);
  CHECK(!drewText(idle, "on your sleep screen now"));
}

// The confirm carries BOTH halves and says what deleting costs. The consequence
// text itself is proved over all four of its combinations in
// host-tests/wallpapers; this is that it reaches the panel at all.
void testWallpapersConfirmSaysTheCostAndOffersBoth() {
  Rendered out;
  wallpapersui::ConfirmModel model;
  model.name = "Bauhaus";
  // Held in a named local: c_str() on the temporary would dangle before the
  // builder ever read it, and the panel would draw whatever was left on the
  // stack -- which is exactly the class of bug toybox::detail::OwnedDevice
  // exists for.
  const std::string cost = wallpapers::deleteConsequence(true, true);
  model.consequence = cost.c_str();
  buildWallpapersConfirm(out, model);
  CHECK(drewText(out, "DELETE WALLPAPER"));
  CHECK(drewText(out, "Bauhaus"));
  CHECK(drewText(out, "whole set again"));
  CHECK(drewText(out, "KEEP IT"));
  CHECK(drewText(out, "DELETE IT"));
  CHECK(registered(out, wallpapersui::ActionKeep));
  CHECK(registered(out, wallpapersui::ActionConfirmDelete));
}

// A wallpaper the user added is named by its FILE, and the sheet must SHRINK
// that name rather than mark it: no Toybox cut above toybox_10 carries U+2026,
// so an ellipsis there draws as a hole and the name stops with a gap after it.
// wallcaption proves toybox::fittedTitle behaves; this proves buildSheet CALLS
// it, which is the half a helper-only test cannot see -- the builder used a
// bare fitLines at the display cut until this went in.
void testWallpapersSheetShrinksALongNameRatherThanMarkingIt() {
  const auto runFor = [](const Rendered& out, const char* needle, fui::TextStyle& style) {
    for (const auto& run : out.target.texts) {
      if (run.text.find(needle) == std::string::npos) continue;
      style = run.style;
      return true;
    }
    return false;
  };

  Rendered shortName;
  wallpapersui::SheetModel sm;
  sm.name = "Bauhaus";
  buildWallpapersSheet(shortName, sm);
  fui::TextStyle shortStyle{};
  CHECK(runFor(shortName, "Bauhaus", shortStyle));
  CHECK(shortStyle.font == fui::FONT_SLOT_TITLE);  // a name that fits keeps the display cut

  Rendered longName;
  const char* huge = "supercalifragilisticexpialidociouswallpaperfromaphone";
  sm.name = huge;
  buildWallpapersSheet(longName, sm);
  fui::TextStyle longStyle{};
  bool found = false;
  std::string drawn;
  for (const auto& run : longName.target.texts) {
    if (run.text.compare(0, 6, "superc") != 0) continue;
    longStyle = run.style;
    drawn = run.text;
    found = true;
  }
  CHECK(found);
  // Either it kept the whole name (by stepping down), or it marked it -- and if
  // it marked it, only in the one cut that can draw the mark.
  CHECK(drawn == huge || longStyle.font == fui::FONT_SLOT_SMALL);
  // It must not still be sitting on the display cut untouched and overflowing.
  CHECK(!(drawn == huge && longStyle.font == fui::FONT_SLOT_TITLE));
}

// The whole defence against same-pixel-different-action, asserted on the rects
// the builders actually draw into rather than on the ones they were meant to.
// wallcaption proves the same identity against the published helpers; this
// proves the SHEET AND THE CONFIRM USE THEM, which is the half a helper-only
// test cannot see.
void testWallpapersConfirmReusesTheSheetsDeletePixelsForItsSafeHalf() {
  const fui::DeviceContext ctx = device();
  const fui::Rect sheetDelete = wallpapersui::sheetDeleteRect(ctx);
  const fui::Rect kill = wallpapersui::confirmDeleteRect(ctx);

  const auto rectOf = [](const Rendered& out, fui::ActionId action, fui::Rect& found) {
    for (size_t i = 0; i < out.interactions.count(); ++i) {
      if (out.interactions.data()[i].action != action) continue;
      found = out.interactions.data()[i].rect;
      return true;
    }
    return false;
  };

  Rendered sheet;
  wallpapersui::SheetModel sm;
  sm.name = "Bauhaus";
  buildWallpapersSheet(sheet, sm);
  fui::Rect drawnSheetDelete{};
  CHECK(rectOf(sheet, wallpapersui::ActionDelete, drawnSheetDelete));
  CHECK(drawnSheetDelete.x == sheetDelete.x && drawnSheetDelete.y == sheetDelete.y &&
        drawnSheetDelete.width == sheetDelete.width && drawnSheetDelete.height == sheetDelete.height);

  Rendered confirm;
  wallpapersui::ConfirmModel cm;
  cm.name = "Bauhaus";
  cm.consequence = "x";
  buildWallpapersConfirm(confirm, cm);
  fui::Rect drawnKeep{};
  fui::Rect drawnKill{};
  CHECK(rectOf(confirm, wallpapersui::ActionKeep, drawnKeep));
  CHECK(rectOf(confirm, wallpapersui::ActionConfirmDelete, drawnKill));

  // A second press of the pixels that opened this screen CANCELS.
  CHECK(drawnKeep.x == drawnSheetDelete.x && drawnKeep.y == drawnSheetDelete.y &&
        drawnKeep.width == drawnSheetDelete.width && drawnKeep.height == drawnSheetDelete.height);
  // And the destructive button is somewhere else entirely.
  CHECK(drawnKill.y == kill.y);
  CHECK(!(drawnKill.y < drawnSheetDelete.y + drawnSheetDelete.height &&
          drawnSheetDelete.y < drawnKill.y + drawnKill.height));
}

// --- READING ----------------------------------------------------------------

template <typename Model, void (*Build)(toybox::Screen&, const Model&)>
void buildStats(Rendered& out, const Model& model) {
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, device(), noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  Build(screen, model);
}

// The month is one hit target resolved arithmetically, because 31 cells do
// not fit the interaction table. calendarDayAt has to invert calendarCell for
// every layout a month can take, or a tap reports the day next to it.
void testTheDayYouTapIsTheDayTheCalendarDrew() {
  const fui::Rect grid = statsui::calendarGrid(device());
  bool allMatch = true;
  for (int first = 0; first < 7; ++first) {
    for (const int days : {28, 29, 30, 31}) {
      for (int day = 1; day <= days; ++day) {
        const int index = first + day - 1;
        const fui::Rect cell = statsui::calendarCell(grid, index / 7, index % 7);
        const int corners[4][2] = {{cell.x, cell.y},
                                   {cell.right() - 1, cell.y},
                                   {cell.x, cell.bottom() - 1},
                                   {cell.right() - 1, cell.bottom() - 1}};
        for (const auto& p : corners) {
          if (statsui::calendarDayAt(device(), first, days, p[0], p[1]) != day) allMatch = false;
        }
      }
      if (first > 0) {
        const fui::Rect blank = statsui::calendarCell(grid, 0, 0);
        CHECK(statsui::calendarDayAt(device(), first, days, blank.x + 5, blank.y + 5) == 0);
      }
    }
  }
  CHECK(allMatch);
  CHECK(statsui::calendarDayAt(device(), 0, 31, grid.x - 1, grid.y) == 0);
  CHECK(statsui::calendarDayAt(device(), 0, 31, grid.x, grid.bottom()) == 0);
  CHECK(grid.bottom() < device().height - toybox::kPillHeight - toybox::kMargin);
}

void testReadingScreensKeepEveryControlTappable() {
  {
    statsui::HomeModel model;
    model.goalMinutes = 30;
    model.todayMs = 12 * 60000;
    model.bookCount = 2;
    for (int i = 0; i < statsui::kHistoryDays; ++i) model.historyMs[i] = static_cast<uint32_t>(i * 4 * 60000);
    Rendered out;
    buildStats<statsui::HomeModel, statsui::buildHome>(out, model);
    CHECK(!out.interactions.overflowed());
    CHECK(out.has(statsui::ActionCalendar) && out.has(statsui::ActionBooks) && out.has(statsui::ActionSettings));
    CHECK(out.target.find("12 MIN") != nullptr);
    CHECK(out.target.find("18 MIN TO GOAL   0 PAGES") != nullptr);
  }
  {
    statsui::CalendarModel model;
    model.firstWeekday = 6;
    model.dayCount = 31;
    model.selected = 31;
    model.canGoNext = true;
    Rendered out;
    buildStats<statsui::CalendarModel, statsui::buildCalendar>(out, model);
    CHECK(!out.interactions.overflowed());
    const int index = model.firstWeekday + 30;
    const fui::Rect last = statsui::calendarCell(statsui::calendarGrid(device()), index / 7, index % 7);
    CHECK(last.bottom() <= statsui::calendarGrid(device()).bottom());
    CHECK(out.tap(last.x + last.width / 2, last.y + last.height / 2).action == statsui::ActionPickDay);
    CHECK(out.has(statsui::ActionPrevMonth) && out.has(statsui::ActionNextMonth));
  }
  {
    fui::ListItem items[statsui::kBookRows] = {};
    const char* titles[statsui::kBookRows] = {"A", "B", "C", "D", "E", "F", "G"};
    for (int i = 0; i < statsui::kBookRows; ++i) {
      items[i].label = titles[i];
      items[i].subtitle = "1 h 02 min";
      items[i].actionValue = static_cast<int16_t>(14 + i);
    }
    statsui::BooksModel model;
    model.items = items;
    model.count = statsui::kBookRows;
    model.canPageOlder = true;
    Rendered out;
    buildStats<statsui::BooksModel, statsui::buildBooks>(out, model);
    CHECK(!out.interactions.overflowed());
    for (int i = 0; i < statsui::kBookRows; ++i) {
      const FakeTarget::TextRun* run = out.target.find(titles[i]);
      CHECK(run != nullptr);
      if (run == nullptr) continue;
      const fui::ActionEvent event = out.tap(run->rect.x + 2, run->rect.y + run->rect.height / 2);
      CHECK(event.action == statsui::ActionOpenBook && event.value == 14 + i);
    }
  }
  {
    statsui::SettingsModel model;
    model.values[statsui::RowGoal] = "30 MIN";
    model.values[statsui::RowTimeLeft] = "CHAPTER";
    model.values[statsui::RowIdle] = "5 MIN";
    Rendered out;
    buildStats<statsui::SettingsModel, statsui::buildSettings>(out, model);
    const char* labels[statsui::kSettingRows] = {"DAILY GOAL", "TIME LEFT", "IDLE LIMIT"};
    for (int i = 0; i < statsui::kSettingRows; ++i) {
      const FakeTarget::TextRun* run = out.target.find(labels[i]);
      CHECK(run != nullptr);
      if (run == nullptr) continue;
      const fui::ActionEvent event = out.tap(run->rect.x + 2, run->rect.y + run->rect.height / 2);
      CHECK(event.action == statsui::ActionCycleSetting && event.value == i);
    }
  }
}

int main() {
  testTheDayYouTapIsTheDayTheCalendarDrew();
  testReadingScreensKeepEveryControlTappable();
  testWallpapersGridHasTwoColumns();
  testWallpapersCellsStayOnScreen();
  testWallpapersCellHitTestMatchesDraw();
  testWallpapersChromeSaysTapToSetWhenNothingIsSet();
  testWallpapersChromeIsQuietWhenSomethingIsSet();
  testWallpapersChromeShowsThePage();
  testWallpapersChromeWarningVerbatim();
  testWallpapersChromeSaysWhenLiveIsTheSleepScreen();
  testWallpapersChromeLiveDoesNotDisplaceTheNoteOrTheWarning();
  testWallpapersEmptyStateSaysSomething();
  testWallpapersCaptionNeverCollidesWithArtwork();
  // testWallpapersHelpCardPointsAtTheUploader is NOT here: app/wallqr deleted
  // buildHelp and the test with it. Both sides' remaining wallpapers tests run.
  testWallpapersSheetOffersPreviewAndDelete();
  testWallpapersSheetSaysWhenItIsTheOneInUse();
  testWallpapersConfirmSaysTheCostAndOffersBoth();
  testWallpapersSheetShrinksALongNameRatherThanMarkingIt();
  testWallpapersConfirmReusesTheSheetsDeletePixelsForItsSafeHalf();
  testWallpapersAddScreenDropsAnAddressItCannotStandBehind();
  testWallpapersOfferReachesTheAddFlow();
  testTheHeaderTitleStaysOutOfTheCoveredRows();
  testTheHeaderBandBottomIgnoresTheBezel();
  testTheBandIsAbsoluteWithoutBeingAsked();
  testTheGlassNeverMovesABodyTop();
  testTheHandRolledBodyTopMatchesTheReservedOne();
  testEveryAppsBodyStartsOnTheSameRow();
  testTheWallpapersThumbnailsStayBigEnoughToRead();
  testSearchingAsksNothing();
  testSeatsSayWhatEachPlayerHasDecided();
  testTheRematchShowsBothAnswers();
  testTheRematchBandIsNotTheWayOut();
  testTheLoneWayOutKeepsTheBottomBand();
  testACapsuleThatChangedMeaningWaitsForThePanel();
  testARepaintThatChangedNothingStillAnswers();
  testAnUnshownRebuildDoesNotCountAsShown();
  testTheRevealGateWaitsForOnePaintAndThenLatches();
  testTheSurfaceGateHoldsAChangedMeaningAndPassesAnUnchangedOne();
  testMeaningsMixPositionally();
  testAPublishingBufferDigestsWhatThePanelIsShowing();
  testBeginBuildDigestsThePublishedGenerationNotTheBuildingOne();
  testAnOptionPopupHighlightRepaintStillAnswers();
  testAnOpponentWhoHasGoneTakesTheButtonWithThem();
  testBattleshipStartMenu();
  testBattleshipCapsuleIsOnlyATriggerWhenItSaysSo();
  testBattleshipWaitingCapsuleIsNotDithered();
  testBattleshipPlacementControls();
  testHnReaderFooter();
  testHnReaderDisabledControls();
  testHnReaderSwapLabelFollowsMode();
  testHnReaderTextStaysInItsRect();
  testHnNotice();
  testHnEveryNoticeCarriesAWayOff();
  testHnList();
  testHnEmptyFrontPageOffersAWayOnward();
  testHnEmptyStateStacksWithoutOverlap();
  testHnFitLines();
  testHnReaderShowsWhereYouAre();
  testHnReaderSaveFailedToastStaysOnTheReader();
  testHnSaveMarkIsLoudestWhenSaved();
  testHnAThreadCanBeKept();
  testShelfFolderDrawsItsOwnNameAndRows();
  testShelfFolderMarksNoRow();
  testToyboxRowGeometryIsWhatTheListActuallyUses();
  testShelfIconsFollowTheRowsWhenTheListScrolls();
  testTheHeaderBandOpensAndClosesTheChooser();
  testThePageCounterClearsTheCorner();
  testTheChooserKeepsTheSamePageGeometry();
  testTheChooserDrawsABoxPerRowAndTicksTheShownOnes();
  testTheChooserWordsFitTheirBands();
  testAChooserRowTogglesInsteadOfOpening();
  testAnEmptyFolderIsItsOwnWayBack();
  testTheShelfPagesWhenAFolderOverflows();
  testAPageStepMovesExactlyOnePage();
  testTheShelfStepStopsAtBothEnds();
  testAFolderComesBackToThePageItWasLeftOn();
  testThePageMarksReadAsAControl();
  testARowOnARestoredPageOpensItsOwnGame();
  testAFolderWithoutADeviceNameHasNoFooter();
  testTheShelfFooterIsADoorWithAFaceOnIt();
  testPlayerOffersThreeSeparateWords();
  testPlayerWordsTileTheRowWithoutGapsOrOverlap();
  testPlayerDrawsTheFaceItsNameDescribes();
  testPlayerBackLeaves();
  testEveryWordHasTheArtworkItNames();
  testAnUnreadableNameDrawsThePlainHead();
  testBothSeatsWearTheirOwnFace();
  testStudyDeckLeadsWithTheCount();
  testStudyHeadlineIsTheHitTarget();
  testStudyDeckRowSwitchesOnlyWhenThereIsSomewhereToGo();
  testStudyOffersNothingWhenNothingIsDue();
  testStudyForecastBarsStayInsideTheirPanel();
  testStudyRecordShowsTheStreak();
  testStudyPanelSaysSoWhenItHasNothing();
  testStudyWarnsWhenAReviewDidNotSave();

  testANarrowerPanelIsNotDrawnFromTheWiderPanelsWrap();
  testTheHackerNewsReaderAlsoWrapsOncePerDocument();
  testFitLinesCutsAnUnbreakableTokenRatherThanVanishing();

  testInkCentredPutsTheInkInTheMiddleOfAnyBox();
  testAShortBoxIsWhatMakesTheCorrectionNecessary();
  solitaireDrawsOneRuleAndClearsIt();
  theChromeProbeCatchesEveryDrawKind();
  everyBandCarriesItsRule();

  std::printf("chrome probe: %d header renders measured, %d renders had no band\n", chromeScreensMeasured,
              chromeScreensSkipped);
  // The probe measuring nothing is a silent regression, not a pass. This number
  // only goes up as screens are added; if it collapses, the renders stopped
  // drawing chrome and the probe quietly stopped being a check.
  check(chromeScreensMeasured >= 130, "the chrome probe measured the suite's header renders", __LINE__);
  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
