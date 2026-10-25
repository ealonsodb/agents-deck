#pragma once
// A status bar across the top of any layout, like a phone's: the layout draws
// underneath it, in the rest of the screen, on every screen.
//
//   left   the time (the deck's clock in the board's time zone), then the
//          agents: working, needs you (orange), the rest
//   right  the cost and output tokens of the agents on the deck, how the
//          board is connected (USB or the Wi-Fi mark) and to which machine,
//          and the battery when the board can measure it
//
// The time zone is the C library's (TZ): a board sets it with setenv("TZ")
// and tzset().
//
// With ShadeHooks, the bar pulls down into the board's settings
// (settings_shade.h): drag it down, or tap it. The settings work whether or
// not the deck is there, so a board whose Wi-Fi fails can still be set up.
// Without hooks, touch in the bar goes to the layout's top row.
//
//   static const BigPanel inner(specBig800(StatusBar::kHeight));
//   static const StatusBar layout(inner, readBattery, &settingsHooks);
//
// Pure C++ (no Arduino).
#include <stdio.h>
#include <time.h>

#include "canvas.h"
#include "deck_ui.h"
#include "font_data.h"
#include "model.h"
#include "panel_layout.h"
#include "round_geom.h"
#include "settings_shade.h"
#include "view.h"

class StatusBar : public PanelLayout {
 public:
  static constexpr int kHeight = 28;

  // battery: the charge in percent, or -1 when unknown; nullptr for a board without one.
  // shade: what the settings can do; nullptr for no settings.
  StatusBar(const PanelLayout& inner, int (*battery)() = nullptr, const ShadeHooks* shade = nullptr)
      : L(inner), battery_(battery), hooks_(shade), T(deckTheme(inner.trueBlack())) {}

  int width() const override { return L.width(); }
  int height() const override { return L.height() + kHeight; }
  bool trueBlack() const override { return L.trueBlack(); }
  bool touchesOffline() const override { return hooks_ != nullptr; }

  void draw(Canvas& c, const PanelInput& in) const override {
    live_ = in.link == LinkState::Live && in.fleet;
    const int H = c.h;
    if (sh_.page != ShadePage::Closed) {
      shadePoll(sh_, hooks_);
      shadeDraw(c, T, sh_, wifiNow(), in.device, in.link, in.ui, kHeight, in.ms);
    } else {
      Canvas below{c.px + kHeight * c.w, c.w, c.h - kHeight};
      L.draw(below, in);
      if (pulling_ && pullY_ > kHeight) {           // the settings coming down with the finger
        Canvas reveal{c.px, c.w, pullY_ < H ? pullY_ : H};
        ShadeState peek = sh_;
        peek.page = ShadePage::Main;
        shadeDraw(reveal, T, peek, wifiNow(), in.device, in.link, nullptr, kHeight, in.ms);
        reveal.rect(0, reveal.h - 2, c.w, 2, T.dim);
      }
    }
    drawBar(c, in);
  }

  DeckCommand touch(PanelUi& ui, const Fleet& f, bool down, int x, int y, DeckScope scope,
                    uint32_t ms) const override {
    if (!hooks_) return L.touch(ui, f, down, x, y < kHeight ? 0 : y - kHeight, scope, ms);
    if (sh_.page != ShadePage::Closed) {             // the settings take every touch
      ui.down = down;
      shadeTouch(sh_, hooks_, wifiNow(), width(), kHeight, height(), down, x, y, ms);
      return DeckCommand();
    }
    if (down && !ui.down && !pulling_ && y < kHeight + 8) {   // a finger on the bar
      pulling_ = true;
      pullStartY_ = y;
    }
    if (pulling_) {
      ui.down = down;
      if (down) { pullY_ = y; return DeckCommand(); }
      pulling_ = false;
      const int dy = pullY_ - pullStartY_;
      if (dy > 80 || (dy < 12 && dy > -12)) shadeOpen(sh_);   // pulled far enough, or tapped
      pullY_ = 0;
      return DeckCommand();
    }
    if (!live_) {                                    // no deck: only the bar works
      ui.down = down;
      return DeckCommand();
    }
    return L.touch(ui, f, down, x, y - kHeight, scope, ms);
  }
  bool tick(PanelUi& ui, const Fleet& f, uint32_t ms) const override {
    return L.tick(ui, f, ms) || pulling_ || sh_.down;
  }

  // The settings' state (tests).
  const ShadeState& shade() const { return sh_; }
  void button(PanelUi& ui, const Fleet& f) const override { L.button(ui, f); }
  void result(PanelUi& ui, uint32_t seq, uint32_t code, uint32_t ms) const override { L.result(ui, seq, code, ms); }

  // Counts and totals over the agents the board keeps.
  struct Totals {
    int working = 0, waiting = 0, other = 0;
    double cost = 0;
    uint64_t tokens = 0;
  };
  static Totals totals(const Fleet* f) {
    Totals t;
    if (!f) return t;
    for (int i = 0; i < f->count; i++) {
      const Agent& a = f->agents[i];
      if (a.status == Status::Waiting) t.waiting++;
      else if (a.status == Status::Running || a.status == Status::Starting) t.working++;
      else t.other++;
      t.cost += a.costUsd;
      t.tokens += a.tokensOut;
    }
    return t;
  }

  // 1234 -> "1.2k", 3100000 -> "3.1m".
  static void compact(uint64_t n, char* out, size_t cap) {
    if (n >= 1000000) snprintf(out, cap, "%.1fm", n / 1e6);
    else if (n >= 1000) snprintf(out, cap, "%.1fk", n / 1e3);
    else snprintf(out, cap, "%u", (unsigned)n);
  }

 private:
  const PanelLayout& L;
  int (*battery_)();
  const ShadeHooks* hooks_;
  const Theme T;
  // The settings and the pull: UI state, like PanelUi, kept here because the
  // layout is the one place both drawing and touch reach.
  mutable ShadeState sh_;
  mutable bool pulling_ = false;
  mutable int pullY_ = 0, pullStartY_ = 0;
  mutable bool live_ = false;

  ShadeWifi wifiNow() const { return hooks_ && hooks_->wifi ? hooks_->wifi() : ShadeWifi(); }

  // A dot and a number; returns the x after it.
  int count(Canvas& c, int x, int y, int n, uint16_t dot, uint16_t ink) const {
    char buf[12];
    snprintf(buf, sizeof buf, "%d", n);
    c.rect(x, y + 6, 6, 6, dot);
    return x + 9 + c.text(FONT_S, x + 9, y, buf, ink) + 10;
  }

  void drawBattery(Canvas& c, int right, int y, int pct) const {
    pct = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    char buf[16];
    snprintf(buf, sizeof buf, "%d%%", pct);
    const int tw = (int)utf8Len(buf) * FONT_S.cellW, bw = 18, bh = 10;
    const int bx = right - bw - 2, by = y + 4;
    c.text(FONT_S, bx - 4 - tw, y, buf, T.ink);
    c.frame(bx, by, bw, bh, T.ink);
    c.rect(bx + bw, by + 3, 2, 4, T.ink);                                   // the cap
    const int fill = (bw - 4) * pct / 100;
    c.rect(bx + 2, by + 2, fill, bh - 4, pct <= 15 ? T.accent : T.ink);
  }

  void drawBar(Canvas& c, const PanelInput& in) const {
    c.rect(0, 0, c.w, kHeight, T.bg);
    c.rect(0, kHeight - 1, c.w, 1, T.line);
    const int pad = 10, y = (kHeight - FONT_S.cellH) / 2;

    // Left: the time, then the agents.
    int x = pad;
    if (in.nowEpoch > 0) {
      const time_t now = (time_t)in.nowEpoch;
      struct tm tm;
      localtime_r(&now, &tm);
      char hm[16];
      snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
      x += c.text(FONT_L, x, (kHeight - FONT_L.cellH) / 2, hm, T.ink) + 16;
    }
    if (in.fleet && in.link == LinkState::Live) {
      const Totals t = totals(in.fleet);
      x = count(c, x, y, t.working, T.ink, T.ink);
      x = count(c, x, y, t.waiting, T.accent, t.waiting ? T.accent : T.dim);
      count(c, x, y, t.other, T.dim, T.dim);
    }

    // Right, from the edge in: battery, link, machine, cost and tokens.
    int r = c.w - pad;
    if (battery_) {
      const int pct = battery_();
      if (pct >= 0) {
        drawBattery(c, r, y, pct);
        r -= 18 + 2 + 4 + 4 * FONT_S.cellW + 14;
      }
    }
    const bool wifi = in.net && in.net->via == NetVia::Wifi;
    const bool live = in.link == LinkState::Live;
    if (wifi) {
      drawNetMark(c, (float)(r - 8), (float)(kHeight / 2 + 6), live ? T.ink : T.dim);
      r -= 16 + 10;
    } else {
      r -= c.text(FONT_S, r - 3 * FONT_S.cellW, y, "USB", live ? T.ink : T.dim) + 10;
    }
    if (in.fleet && in.fleet->server[0]) {
      const int w = (int)utf8Len(in.fleet->server) * FONT_S.cellW;
      const int room = w < 160 ? w : 160;
      c.text(FONT_S, r - room, y, in.fleet->server, T.dim, room);
      r -= room + 14;
    }
    if (in.fleet && live) {
      const Totals t = totals(in.fleet);
      char tok[12], buf[32];
      compact(t.tokens, tok, sizeof tok);
      snprintf(buf, sizeof buf, "$%.2f" DECK_DOT "%s tok", t.cost, tok);
      const int w = (int)utf8Len(buf) * FONT_S.cellW;
      if (r - w > x + 16) c.text(FONT_S, r - w, y, buf, T.dim);
    }
  }
};
