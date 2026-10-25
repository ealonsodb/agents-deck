#pragma once
// The settings that pull down from the status bar (status_bar.h), like a
// phone's: the board's Wi-Fi and a restart.
//
//   main      the saved network and how it's doing, choose network, forget
//             (tap twice), restart (tap twice), close
//   networks  the networks in range, strongest first, a lock on the ones with
//             a password; tap one to join it
//   password  an on-screen keyboard (letters, two pages of symbols, shift,
//             show or hide), then connect
//
// Setting the network here needs the board in your hand, like the USB cable:
// nothing of it is reachable over the network (net_link.h's localJoin). The
// board supplies what it can do through ShadeHooks.
//
// Every button comes from shadeControls(), which both drawing and touch use.
// Pure C++ (no Arduino).
#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "deck_ui.h"
#include "font_big.h"
#include "font_data.h"
#include "layout_big.h"
#include "net_model.h"
#include "panel_layout.h"

// A network in range.
struct ShadeNet {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

// What the board's Wi-Fi is doing (net_link.h's LocalWifi, as plain data).
struct ShadeWifi {
  char ssid[33] = "";      // the saved network
  bool capable = false;    // the firmware has Wi-Fi
  bool paired = false;     // it has a Wi-Fi certificate
  bool testing = false;    // trying a network it was just given
  NetVia via = NetVia::Usb;
  NetPhase phase = NetPhase::Off;
  uint8_t lastTest = 0;    // on USB, the last try: 0 none, 1 worked, 2 wrong password, 3 not found, 4 failed
  char ip[16] = "";
  int8_t rssi = 0;
};

// What the board can do. Any of them may be nullptr.
struct ShadeHooks {
  ShadeWifi (*wifi)();
  void (*scan)();                                   // start looking for networks
  int (*networks)(ShadeNet* out, int max);          // what it found; -1 while it looks
  bool (*join)(const char* ssid, const char* password);
  void (*forget)();
  void (*restart)();
};

enum class ShadePage : uint8_t { Closed, Main, Networks, Password };

enum class Ctl : uint8_t {
  None, Close, Choose, Forget, Restart,           // main
  Back, Rescan, Net,                              // networks (and back on password)
  Key, Shift, Mode, Backspace, Space, Show, Connect,  // the keyboard
};

struct Control {
  Ctl id = Ctl::None;
  int index = 0;           // Net: which network; Key: the character
  Rect r{0, 0, 0, 0};
};

constexpr int kShadeNets = 10;
constexpr int kShadeArmMs = 3000;

struct ShadeState {
  ShadePage page = ShadePage::Closed;
  // The finger.
  bool down = false;
  int x0 = 0, y0 = 0, x = 0, y = 0;
  Control pressed;
  // Networks.
  ShadeNet nets[kShadeNets];
  int netCount = -1;       // -1: looking
  // Password.
  char ssid[33] = "";
  char pass[65] = "";
  bool shift = false, symbols = false, symPage2 = false, show = false;
  // Tap twice.
  uint32_t forgetArmedUntil = 0, restartArmedUntil = 0;
  // A short note at the bottom ("couldn't save that network").
  char note[48] = "";
  uint32_t noteUntil = 0;
};

namespace shade_detail {

constexpr int kPad = 24, kBtnH = 44, kRowH = 44, kRowGap = 6, kKeyH = 72, kKeyGap = 8;

// The keyboard's rows of characters: letters (row 3 sits between shift and backspace).
static inline const char* keyRow(const ShadeState& s, int row) {
  static const char* lower[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static const char* upper[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  static const char* sym1[] = {"1234567890", "-/:;()$&@", ".,?!'\"_"};
  static const char* sym2[] = {"[]{}#%^*+=", "_\\|~<>`$&", ".,?!'\"@"};
  if (s.symbols) return (s.symPage2 ? sym2 : sym1)[row];
  return (s.shift ? upper : lower)[row];
}

static inline int signalPct(int8_t rssi) {
  const int p = (rssi + 100) * 2;
  return p < 0 ? 0 : p > 100 ? 100 : p;
}

static inline int shadeTextW(const Font& f, const char* s) { return (int)utf8Len(s) * f.cellW; }

}  // namespace shade_detail

// Every button on the page, in screen pixels (the shade starts at `top`).
static inline int shadeControls(const ShadeState& s, const ShadeWifi& w, int W, int top, int H, Control* out,
                                int max) {
  using namespace shade_detail;
  int n = 0;
  auto add = [&](Ctl id, Rect r, int index = 0) {
    if (n < max) out[n++] = Control{id, index, r};
  };
  switch (s.page) {
    case ShadePage::Closed:
      break;
    case ShadePage::Main: {
      add(Ctl::Close, Rect{W - kPad - 120, top + 10, 120, 40});
      const Rect wifi{kPad, top + 64, W - 2 * kPad, 176};
      if (w.capable) {
        add(Ctl::Choose, Rect{wifi.x + 16, wifi.y + wifi.h - kBtnH - 14, 240, kBtnH});
        if (w.ssid[0]) add(Ctl::Forget, Rect{wifi.x + 16 + 240 + 12, wifi.y + wifi.h - kBtnH - 14, 260, kBtnH});
      }
      const Rect board{kPad, top + 256, W - 2 * kPad, 104};
      add(Ctl::Restart, Rect{board.x + board.w - 16 - 260, board.y + (board.h - kBtnH) / 2, 260, kBtnH});
      break;
    }
    case ShadePage::Networks: {
      add(Ctl::Back, Rect{kPad, top + 10, 110, 40});
      add(Ctl::Rescan, Rect{W - kPad - 130, top + 10, 130, 40});
      const int y0 = top + 64, rows = (H - y0 - 8) / (kRowH + kRowGap);
      for (int i = 0; i < s.netCount && i < rows; i++)
        add(Ctl::Net, Rect{kPad, y0 + i * (kRowH + kRowGap), W - 2 * kPad, kRowH}, i);
      break;
    }
    case ShadePage::Password: {
      add(Ctl::Back, Rect{kPad, top + 10, 110, 40});
      const int kw = (W - 2 * kPad - 9 * kKeyGap) / 10, y0 = H - 4 * kKeyH - 3 * kKeyGap - 8;
      const int full = 10 * kw + 9 * kKeyGap, x0 = (W - full) / 2;
      for (int row = 0; row < 3; row++) {
        const char* chars = keyRow(s, row);
        const int len = (int)strlen(chars), y = y0 + row * (kKeyH + kKeyGap);
        int x = x0 + (row == 1 ? (kw + kKeyGap) / 2 : 0);
        if (row == 2) {
          const int side = (full - len * kw - (len + 1) * kKeyGap) / 2;
          add(Ctl::Shift, Rect{x0, y, side, kKeyH});
          x = x0 + side + kKeyGap;
          for (int i = 0; i < len; i++, x += kw + kKeyGap) add(Ctl::Key, Rect{x, y, kw, kKeyH}, (unsigned char)chars[i]);
          add(Ctl::Backspace, Rect{x0 + full - side, y, side, kKeyH});
          continue;
        }
        for (int i = 0; i < len; i++, x += kw + kKeyGap) add(Ctl::Key, Rect{x, y, kw, kKeyH}, (unsigned char)chars[i]);
      }
      const int y = y0 + 3 * (kKeyH + kKeyGap), mode = 140, show = 140, go = 180;
      const int space = full - mode - show - go - 3 * kKeyGap;
      add(Ctl::Mode, Rect{x0, y, mode, kKeyH});
      add(Ctl::Space, Rect{x0 + mode + kKeyGap, y, space, kKeyH}, ' ');
      add(Ctl::Show, Rect{x0 + mode + space + 2 * kKeyGap, y, show, kKeyH});
      add(Ctl::Connect, Rect{x0 + full - go, y, go, kKeyH});
      break;
    }
  }
  return n;
}

static inline Control shadeControlAt(const ShadeState& s, const ShadeWifi& w, int W, int top, int H, int x, int y) {
  Control cs[64];
  const int n = shadeControls(s, w, W, top, H, cs, 64);
  for (int i = 0; i < n; i++)
    if (cs[i].r.contains(x, y)) return cs[i];
  return Control();
}

// The line under the saved network: how it's doing.
static inline void shadeWifiState(const ShadeWifi& w, char* out, size_t cap) {
  using shade_detail::signalPct;
  if (!w.capable) { snprintf(out, cap, "no Wi-Fi in this firmware: flash the idf5 build"); return; }
  if (!w.ssid[0]) { snprintf(out, cap, "choose a network for when it's away from the Mac"); return; }
  if (w.testing) { snprintf(out, cap, "trying it\xE2\x80\xA6"); return; }
  if (w.via == NetVia::Wifi) {
    switch (w.phase) {
      case NetPhase::Joining:         snprintf(out, cap, "joining\xE2\x80\xA6"); return;
      case NetPhase::Connecting:      snprintf(out, cap, "joined, reaching the deck\xE2\x80\xA6"); return;
      case NetPhase::WrongPassword:   snprintf(out, cap, "wrong password"); return;
      case NetPhase::NoNetwork:       snprintf(out, cap, "network not found"); return;
      case NetPhase::DeckUnreachable: snprintf(out, cap, "deck not reachable"); return;
      case NetPhase::CertRejected:    snprintf(out, cap, "certificate rejected: pair again over USB"); return;
      case NetPhase::WifiFailed:      snprintf(out, cap, "Wi-Fi failed"); return;
      default:
        snprintf(out, cap, "connected" DECK_DOT "%s" DECK_DOT "signal %d%%", w.ip, signalPct(w.rssi));
        return;
    }
  }
  switch (w.lastTest) {
    case 1:  snprintf(out, cap, "works (%s): used when it's away from the Mac", w.ip); return;
    case 2:  snprintf(out, cap, "wrong password"); return;
    case 3:  snprintf(out, cap, "network not found"); return;
    case 4:  snprintf(out, cap, "Wi-Fi failed"); return;
    default: snprintf(out, cap, "saved: used when it's away from the Mac"); return;
  }
}

// ---------------------------------------------------------------- drawing

static inline void shadeButton(Canvas& c, const Theme& T, const Rect& r, const char* label, bool pressed,
                               bool primary = false, bool armed = false) {
  using namespace big_detail;
  const uint16_t fill = primary || armed ? T.accent : pressed ? T.focusedFill : T.panel;
  const uint16_t ink = primary || armed ? T.bg : T.ink;
  rrect(c, r, 10, fill);
  if (!primary && !armed) rframe(c, r, 10, 1, pressed ? T.ink : kRowOutline);
  const Font& f = FONT_M13;
  const int w = shade_detail::shadeTextW(f, label);
  c.text(f, r.x + (r.w - w) / 2, r.y + (r.h - f.cellH) / 2, label, ink, r.w - 8);
}

static inline void shadeLock(Canvas& c, int x, int y, uint16_t col) {
  c.frame(x + 2, y, 8, 7, col);          // the shackle
  c.rect(x, y + 6, 12, 9, col);          // the body
}

static inline void shadeBars(Canvas& c, int x, int y, int pct, const Theme& T) {
  for (int i = 0; i < 4; i++) {
    const int h = 4 + i * 4;
    c.rect(x + i * 6, y + 16 - h, 4, h, pct > i * 25 ? T.ink : T.line);
  }
}

static inline void shadeDraw(Canvas& c, const Theme& T, const ShadeState& s, const ShadeWifi& w, const char* device,
                             LinkState link, const PanelUi* ui, int top, uint32_t ms) {
  using namespace shade_detail;
  using namespace big_detail;
  const int W = c.w, H = c.h;
  c.rect(0, top, W, H - top, T.bg);
  Control cs[64];
  const int n = shadeControls(s, w, W, top, H, cs, 64);
  auto pressed = [&](const Control& k) {
    return s.down && s.pressed.id == k.id && s.pressed.index == k.index && s.pressed.id != Ctl::None;
  };
  char buf[96];

  if (s.page == ShadePage::Main) {
    c.text(FONT_B20, kPad, top + 16, "settings", T.ink);
    const Rect wifi{kPad, top + 64, W - 2 * kPad, 176};
    rrect(c, wifi, 12, T.panel);
    c.text(FONT_B15, wifi.x + 16, wifi.y + 14, "Wi-Fi", T.ink);
    c.text(FONT_M13, wifi.x + 16, wifi.y + 46, w.ssid[0] ? w.ssid : "no network", w.ssid[0] ? T.ink : T.dim,
           wifi.w - 32);
    shadeWifiState(w, buf, sizeof buf);
    const bool bad = w.capable && w.ssid[0] && !w.testing &&
                     (w.via == NetVia::Wifi ? (w.phase == NetPhase::WrongPassword || w.phase == NetPhase::NoNetwork ||
                                               w.phase == NetPhase::WifiFailed || w.phase == NetPhase::CertRejected ||
                                               w.phase == NetPhase::DeckUnreachable)
                                            : w.lastTest >= 2);
    c.text(FONT_S, wifi.x + 16, wifi.y + 70, buf, bad ? T.accent : T.dim, wifi.w - 32);
    if (w.capable && !w.paired)
      c.text(FONT_S, wifi.x + 16, wifi.y + 90, "not paired yet: plug it into the Mac once to pair it", T.accent,
             wifi.w - 32);
    const Rect board{kPad, top + 256, W - 2 * kPad, 104};
    rrect(c, board, 12, T.panel);
    c.text(FONT_B15, board.x + 16, board.y + 14, "board", T.ink);
    c.text(FONT_M13, board.x + 16, board.y + 44, device && device[0] ? device : "\xE2\x80\x94", T.ink, board.w - 320);
    const char* how = link == LinkState::Live          ? (w.via == NetVia::Wifi ? "on the deck over Wi-Fi" : "on the deck over USB")
                      : link == LinkState::Unapproved ? "waiting for approval"
                      : link == LinkState::Lost       ? "deck offline"
                                                      : "connecting to the deck";
    c.text(FONT_S, board.x + 16, board.y + 70, how, T.dim, board.w - 320);
    for (int i = 0; i < n; i++) {
      const Control& k = cs[i];
      switch (k.id) {
        case Ctl::Close:   shadeButton(c, T, k.r, "close", pressed(k)); break;
        case Ctl::Choose:  shadeButton(c, T, k.r, w.ssid[0] ? "change network" : "choose network", pressed(k)); break;
        case Ctl::Forget:
          shadeButton(c, T, k.r, ms < s.forgetArmedUntil ? "tap again to forget" : "forget", pressed(k), false,
                      ms < s.forgetArmedUntil);
          break;
        case Ctl::Restart:
          shadeButton(c, T, k.r, ms < s.restartArmedUntil ? "tap again to restart" : "restart", pressed(k), false,
                      ms < s.restartArmedUntil);
          break;
        default: break;
      }
    }
    // The handle: swipe up anywhere to close.
    rrect(c, Rect{W / 2 - 30, H - 14, 60, 5}, 2, T.dim);
  } else if (s.page == ShadePage::Networks) {
    c.text(FONT_B20, kPad + 130, top + 16, "choose a network", T.ink);
    if (s.netCount < 0) {
      const char* t = "looking for networks\xE2\x80\xA6";
      c.text(FONT_M13, (W - shadeTextW(FONT_M13, t)) / 2, top + 160, t, T.dim);
    } else if (s.netCount == 0) {
      const char* t = "no networks found";
      c.text(FONT_M13, (W - shadeTextW(FONT_M13, t)) / 2, top + 160, t, T.dim);
    }
    for (int i = 0; i < n; i++) {
      const Control& k = cs[i];
      if (k.id == Ctl::Back) shadeButton(c, T, k.r, "back", pressed(k));
      else if (k.id == Ctl::Rescan) shadeButton(c, T, k.r, "rescan", pressed(k));
      else if (k.id == Ctl::Net) {
        const ShadeNet& net = s.nets[k.index];
        rrect(c, k.r, 10, pressed(k) ? T.focusedFill : T.panel);
        const bool saved = w.ssid[0] && !strcmp(w.ssid, net.ssid);
        c.text(FONT_M13, k.r.x + 16, k.r.y + (k.r.h - FONT_M13.cellH) / 2, net.ssid, saved ? T.accent : T.ink,
               k.r.w - 120);
        shadeBars(c, k.r.x + k.r.w - 40, k.r.y + 14, signalPct(net.rssi), T);
        if (net.secure) shadeLock(c, k.r.x + k.r.w - 64, k.r.y + 15, T.dim);
      }
    }
  } else if (s.page == ShadePage::Password) {
    snprintf(buf, sizeof buf, "password for %s", s.ssid);
    c.text(FONT_B15, kPad + 130, top + 20, buf, T.ink, W - kPad * 2 - 130);
    const Rect field{kPad, top + 62, W - 2 * kPad, 52};
    rrect(c, field, 10, T.panel);
    rframe(c, field, 10, 1, big_detail::kRowOutline);
    const int len = (int)strlen(s.pass);
    char shown[65];
    if (s.show) {
      snprintf(shown, sizeof shown, "%s", s.pass);
    } else {
      memset(shown, '*', (size_t)len);   // the fonts have no bullet
      shown[len] = '\0';
    }
    const int tx = field.x + 16, ty = field.y + (field.h - FONT_M13.cellH) / 2;
    const int drawn = len ? c.text(FONT_M13, tx, ty, shown, T.ink, field.w - 32, true) : 0;
    if (!len) c.text(FONT_M13, tx, ty, "type the password", T.dim);
    else c.rect(tx + drawn + 2, field.y + 12, 2, field.h - 24, T.accent);   // the caret
    if (len && len < 8) {
      const char* t = "at least 8 characters";
      c.text(FONT_S, field.x + field.w - 16 - shadeTextW(FONT_S, t), field.y + (field.h - FONT_S.cellH) / 2, t, T.dim);
    }
    for (int i = 0; i < n; i++) {
      const Control& k = cs[i];
      const bool p = pressed(k);
      char label[8];
      switch (k.id) {
        case Ctl::Back: shadeButton(c, T, k.r, "back", p); break;
        case Ctl::Key:
        case Ctl::Space:
          if (k.id == Ctl::Space) snprintf(label, sizeof label, "space");
          else snprintf(label, sizeof label, "%c", (char)k.index);
          shadeButton(c, T, k.r, label, p);
          break;
        case Ctl::Shift:
          shadeButton(c, T, k.r, s.symbols ? (s.symPage2 ? "123" : "#+=") : "shift", p, false,
                      !s.symbols && s.shift);
          break;
        case Ctl::Mode: shadeButton(c, T, k.r, s.symbols ? "abc" : "123", p); break;
        case Ctl::Backspace: shadeButton(c, T, k.r, "delete", p); break;
        case Ctl::Show: shadeButton(c, T, k.r, s.show ? "hide" : "show", p); break;
        case Ctl::Connect: shadeButton(c, T, k.r, "connect", p, len >= 8); break;
        default: break;
      }
    }
  }

  // A note: the shade's own, else the board's toast ("trying Wi-Fi…", "Wi-Fi works").
  const char* note = ms < s.noteUntil ? s.note : ui && ms < ui->toastUntil ? ui->toast : nullptr;
  if (note && note[0] && s.page != ShadePage::Password) {
    const int w2 = shadeTextW(FONT_M13, note) + 32, h2 = 40;
    const Rect r{(W - w2) / 2, H - 70, w2, h2};
    rrect(c, r, 20, T.ink);
    c.text(FONT_M13, r.x + 16, r.y + (h2 - FONT_M13.cellH) / 2, note, T.bg);
  }
}

// ---------------------------------------------------------------- touch

static inline void shadeNote(ShadeState& s, const char* text, uint32_t ms) {
  snprintf(s.note, sizeof s.note, "%s", text);
  s.noteUntil = ms + 3000;
}

static inline void shadeOpen(ShadeState& s) {
  s.page = ShadePage::Main;
  s.forgetArmedUntil = s.restartArmedUntil = 0;
  s.down = false;
}

static inline void shadeClose(ShadeState& s) {
  memset(s.pass, 0, sizeof s.pass);
  s.page = ShadePage::Closed;
  s.down = false;
}

static inline void shadeScan(ShadeState& s, const ShadeHooks* h) {
  s.netCount = -1;
  if (h && h->scan) h->scan();
  else s.netCount = 0;
}

// Picks up what a scan found (call it every frame while the networks page is open).
static inline void shadePoll(ShadeState& s, const ShadeHooks* h) {
  if (s.page != ShadePage::Networks || s.netCount >= 0) return;
  if (!h || !h->networks) { s.netCount = 0; return; }
  const int n = h->networks(s.nets, kShadeNets);
  if (n >= 0) s.netCount = n > kShadeNets ? kShadeNets : n;
}

static inline void shadeAct(ShadeState& s, const ShadeHooks* h, const Control& k, uint32_t ms) {
  const size_t len = strlen(s.pass);
  switch (k.id) {
    case Ctl::Close: shadeClose(s); break;
    case Ctl::Choose:
      s.page = ShadePage::Networks;
      shadeScan(s, h);
      break;
    case Ctl::Forget:
      if (ms < s.forgetArmedUntil) {
        s.forgetArmedUntil = 0;
        if (h && h->forget) h->forget();
      } else {
        s.forgetArmedUntil = ms + kShadeArmMs;
      }
      break;
    case Ctl::Restart:
      if (ms < s.restartArmedUntil) {
        s.restartArmedUntil = 0;
        if (h && h->restart) h->restart();
      } else {
        s.restartArmedUntil = ms + kShadeArmMs;
      }
      break;
    case Ctl::Back:
      memset(s.pass, 0, sizeof s.pass);
      s.page = s.page == ShadePage::Password ? ShadePage::Networks : ShadePage::Main;
      break;
    case Ctl::Rescan: shadeScan(s, h); break;
    case Ctl::Net: {
      const ShadeNet& net = s.nets[k.index];
      snprintf(s.ssid, sizeof s.ssid, "%s", net.ssid);
      if (net.secure) {
        memset(s.pass, 0, sizeof s.pass);
        s.shift = s.symbols = s.symPage2 = s.show = false;
        s.page = ShadePage::Password;
      } else {
        const bool ok = h && h->join && h->join(s.ssid, "");
        if (!ok) shadeNote(s, "couldn't save that network", ms);
        s.page = ShadePage::Main;
      }
      break;
    }
    case Ctl::Key:
    case Ctl::Space:
      if (len < sizeof s.pass - 1) {
        s.pass[len] = (char)k.index;
        s.pass[len + 1] = '\0';
      }
      break;
    case Ctl::Backspace:
      if (len) s.pass[len - 1] = '\0';
      break;
    case Ctl::Shift:
      if (s.symbols) s.symPage2 = !s.symPage2;
      else s.shift = !s.shift;
      break;
    case Ctl::Mode:
      s.symbols = !s.symbols;
      s.symPage2 = false;
      break;
    case Ctl::Show: s.show = !s.show; break;
    case Ctl::Connect:
      if (len >= 8) {
        const bool ok = h && h->join && h->join(s.ssid, s.pass);
        memset(s.pass, 0, sizeof s.pass);
        s.page = ShadePage::Main;
        if (!ok) shadeNote(s, "couldn't save that network", ms);
      }
      break;
    default: break;
  }
}

// One touch sample on the open shade. A tap acts on release, on the button
// it started on; a swipe up on the main page closes it.
static inline void shadeTouch(ShadeState& s, const ShadeHooks* h, const ShadeWifi& w, int W, int top, int H, bool down,
                              int x, int y, uint32_t ms) {
  if (down) {
    if (!s.down) {
      s.down = true;
      s.x0 = x;
      s.y0 = y;
      s.pressed = shadeControlAt(s, w, W, top, H, x, y);
    }
    s.x = x;
    s.y = y;
    const int dx = x - s.x0, dy = y - s.y0;
    if (dx * dx + dy * dy > 20 * 20) s.pressed = Control();   // a drag, not a tap
    return;
  }
  if (!s.down) return;
  s.down = false;
  if (s.page == ShadePage::Main && s.y - s.y0 < -60) { shadeClose(s); return; }
  const Control k = s.pressed;
  s.pressed = Control();
  if (k.id != Ctl::None) shadeAct(s, h, k, ms);
}
