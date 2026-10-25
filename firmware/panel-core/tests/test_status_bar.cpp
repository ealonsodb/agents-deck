// status_bar.h: the phone-style bar over the 7" board's layout. Its size,
// the counts and totals, touch moved under the bar, and every screen (bar
// and layout) inside the frame, with and without a battery.
#include <assert.h>
#include <stdlib.h>

#include <vector>

#include "../sim/fixture.h"
#include "layout_big.h"
#include "status_bar.h"

static int battery82() { return 82; }
static int batteryUnknown() { return -1; }
static int batteryOver() { return 250; }


// ---- fake settings hooks
static int scans = 0, forgets = 0, restarts = 0, joins = 0;
static char joinedSsid[33], joinedPass[65];
static bool scanDone = false;
static ShadeWifi fakeWifi() {
  ShadeWifi w;
  w.capable = w.paired = true;
  return w;
}
static void fakeScan() { scans++; scanDone = false; }
static int fakeNets(ShadeNet* out, int max) {
  if (!scanDone) { scanDone = true; return -1; }   // the first poll: still looking
  const ShadeNet nets[] = {{"lab", -50, true}, {"cafe", -70, false}};
  int n = 0;
  for (const ShadeNet& x : nets)
    if (n < max) out[n++] = x;
  return n;
}
static bool fakeJoin(const char* ssid, const char* pass) {
  joins++;
  snprintf(joinedSsid, sizeof joinedSsid, "%s", ssid);
  snprintf(joinedPass, sizeof joinedPass, "%s", pass);
  return true;
}
static void fakeForget() { forgets++; }
static void fakeRestart() { restarts++; }
static const ShadeHooks kHooks{fakeWifi, fakeScan, fakeNets, fakeJoin, fakeForget, fakeRestart};

static void tapAt(const StatusBar& B, PanelUi& ui, const Fleet& f, int x, int y, uint32_t ms) {
  B.touch(ui, f, true, x, y, DeckScope::Act, ms);
  B.touch(ui, f, false, x, y, DeckScope::Act, ms + 40);
}

static bool tapCtl(const StatusBar& B, PanelUi& ui, const Fleet& f, Ctl id, int index, uint32_t ms) {
  Control cs[64];
  const int n = shadeControls(B.shade(), fakeWifi(), B.width(), StatusBar::kHeight, B.height(), cs, 64);
  for (int i = 0; i < n; i++)
    if (cs[i].id == id && (index < 0 || cs[i].index == index)) {
      tapAt(B, ui, f, cs[i].r.x + cs[i].r.w / 2, cs[i].r.y + cs[i].r.h / 2, ms);
      return true;
    }
  return false;
}

static void typeText(const StatusBar& B, PanelUi& ui, const Fleet& f, const char* text, uint32_t ms) {
  // The keyboard's four pages: letters, capitals, symbols, more symbols.
  auto page = [&](bool sym, bool second) {
    if (B.shade().symbols != sym) tapCtl(B, ui, f, Ctl::Mode, -1, ms);
    const bool now = sym ? B.shade().symPage2 : B.shade().shift;
    if (now != second) tapCtl(B, ui, f, Ctl::Shift, -1, ms);
  };
  for (const char* k = text; *k; k++) {
    if (*k == ' ') { assert(tapCtl(B, ui, f, Ctl::Space, -1, ms)); continue; }
    bool typed = false;
    for (int p = 0; p < 4 && !typed; p++) {
      page(p >= 2, p % 2 == 1);
      typed = tapCtl(B, ui, f, Ctl::Key, (unsigned char)*k, ms);
    }
    assert(typed);
  }
}

int main() {
  setenv("TZ", "UTC0", 1);
  tzset();
  const BigSpec S = specBig800(StatusBar::kHeight);
  const BigPanel P(S);
  const StatusBar B(P, battery82);

  // ---- the bar takes the top of the 800 x 480 screen; the layout gets the rest
  assert(S.h == 480 - StatusBar::kHeight);
  assert(B.width() == 800 && B.height() == 480 && !B.trueBlack());

  // ---- counts and totals
  static DeckFleet d;
  assert(fixture::load(FIXTURE, d));
  const StatusBar::Totals t = StatusBar::totals(&d.fleet);
  assert(t.working + t.waiting + t.other == d.fleet.count);
  assert(t.waiting > 0 && t.cost > 0 && t.tokens > 0);
  assert(StatusBar::totals(nullptr).working == 0);
  char buf[16];
  StatusBar::compact(999, buf, sizeof buf);
  assert(!strcmp(buf, "999"));
  StatusBar::compact(248800, buf, sizeof buf);
  assert(!strcmp(buf, "248.8k"));
  StatusBar::compact(3100000, buf, sizeof buf);
  assert(!strcmp(buf, "3.1m"));

  // ---- touch: under the bar, the layout's own coordinates; in the bar, its top row
  {
    PanelUi a, b;
    const BigGrid g = bigGrid(S, bigPages(d.fleet));
    const Rect r = bigCell(g, 5);
    const int x = r.x + r.w / 2, y = r.y + r.h / 2;
    B.touch(a, d.fleet, true, x, y + StatusBar::kHeight, DeckScope::Act, 0);
    B.touch(a, d.fleet, false, 0, 0, DeckScope::Act, 60);
    P.touch(b, d.fleet, true, x, y, DeckScope::Act, 0);
    P.touch(b, d.fleet, false, 0, 0, DeckScope::Act, 60);
    assert(a.screen != Screen::Home && a.screen == b.screen && !strcmp(a.openId, b.openId));
    PanelUi c;
    B.touch(c, d.fleet, true, 400, 5, DeckScope::Act, 0);       // a tap in the bar doesn't crash or go elsewhere
    B.touch(c, d.fleet, false, 0, 0, DeckScope::Act, 60);
  }

  // ---- every screen inside the frame: battery known, unknown, out of range, none; USB and Wi-Fi
  const StatusBar bars[] = {StatusBar(P, battery82), StatusBar(P, batteryUnknown), StatusBar(P, batteryOver),
                            StatusBar(P)};
  const int W = 800, H = 480, PAD = 2 * W;
  const uint16_t GUARD = 0xDEAD;
  std::vector<uint16_t> mem(W * H + 2 * PAD, GUARD);
  Canvas c{mem.data() + PAD, W, H};
  auto guards = [&] {
    for (int i = 0; i < PAD; i++)
      if (mem[i] != GUARD || mem[PAD + W * H + i] != GUARD) return false;
    return true;
  };
  NetView wifi;
  wifi.via = NetVia::Wifi;
  for (const StatusBar& bar : bars)
    for (LinkState ls : {LinkState::Connecting, LinkState::Unapproved, LinkState::Live, LinkState::Lost})
      for (const NetView* net : {(const NetView*)nullptr, (const NetView*)&wifi}) {
        PanelUi ui;
        PanelInput in{&d.fleet, ls, "waveshare-touch-lcd-7-0123456789", DeckScope::Admin, 300,
                      d.fleet.serverTime, &ui, 0, net};
        bar.draw(c, in);
        assert(guards());
        in.fleet = nullptr;
        in.nowEpoch = 0;
        bar.draw(c, in);
        assert(guards());
        in.fleet = &d.fleet;
        for (int i = 0; i < d.fleet.count; i++) {
          bigOpen(ui, d.fleet, i);
          for (Screen s : {Screen::Card, Screen::Prompt}) {
            ui.screen = s;
            bar.draw(c, in);
            assert(guards());
          }
        }
      }

  // ---- the bar is drawn: its bottom line, and the clock's digits in the top-left
  {
    PanelUi ui;
    PanelInput in{&d.fleet, LinkState::Live, "w", DeckScope::Admin, 300, d.fleet.serverTime, &ui, 0};
    B.draw(c, in);
    const Theme T = deckTheme(false);
    assert(c.px[(StatusBar::kHeight - 1) * W + 400] == T.line);
    bool ink = false;
    for (int y = 0; y < StatusBar::kHeight && !ink; y++)
      for (int x = 10; x < 60 && !ink; x++) ink = c.px[y * W + x] == T.ink;
    assert(ink);
  }

  // ---- the settings: pull the bar down, or tap it
  {
    const StatusBar S7(P, nullptr, &kHooks);
    assert(S7.touchesOffline() && !B.touchesOffline());
    PanelUi ui;
    PanelInput in{&d.fleet, LinkState::Live, "w", DeckScope::Act, 0, d.fleet.serverTime, &ui, 0};
    S7.draw(c, in);
    S7.touch(ui, d.fleet, true, 400, 10, DeckScope::Act, 0);          // a short pull: stays closed
    S7.touch(ui, d.fleet, true, 400, 60, DeckScope::Act, 20);
    S7.touch(ui, d.fleet, false, 400, 60, DeckScope::Act, 40);
    assert(S7.shade().page == ShadePage::Closed && !ui.down);
    S7.touch(ui, d.fleet, true, 400, 10, DeckScope::Act, 100);        // pulled down
    S7.touch(ui, d.fleet, true, 400, 250, DeckScope::Act, 120);
    assert(S7.tick(ui, d.fleet, 130));                                 // follows the finger
    S7.draw(c, in);
    assert(guards());
    S7.touch(ui, d.fleet, false, 400, 250, DeckScope::Act, 140);
    assert(S7.shade().page == ShadePage::Main && ui.screen == Screen::Home);
    S7.draw(c, in);
    assert(guards());

    // Every button inside the screen, below the bar, none on top of another; on every page.
    auto checkControls = [&] {
      Control cs[64];
      const int n = shadeControls(S7.shade(), fakeWifi(), 800, StatusBar::kHeight, 480, cs, 64);
      assert(n > 0 && n < 64);
      for (int i = 0; i < n; i++) {
        const Rect& r = cs[i].r;
        assert(r.x >= 0 && r.y >= StatusBar::kHeight && r.x + r.w <= 800 && r.y + r.h <= 480 && r.w > 0 && r.h > 0);
        for (int j = 0; j < i; j++) assert(!r.intersects(cs[j].r));
      }
    };
    checkControls();

    // Restart and forget take two taps.
    assert(tapCtl(S7, ui, d.fleet, Ctl::Restart, -1, 1000) && restarts == 0);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Restart, -1, 1500) && restarts == 1);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Restart, -1, 2000));
    assert(tapCtl(S7, ui, d.fleet, Ctl::Restart, -1, 6000) && restarts == 1);   // too late: armed again
    assert(tapCtl(S7, ui, d.fleet, Ctl::Forget, -1, 7000) == false);            // nothing saved: no forget

    // Choose a network: it scans, lists, and an open one joins at once.
    assert(tapCtl(S7, ui, d.fleet, Ctl::Choose, -1, 8000) && scans == 1);
    assert(S7.shade().page == ShadePage::Networks && S7.shade().netCount < 0);
    S7.draw(c, in);                                                    // still looking
    S7.draw(c, in);
    assert(S7.shade().netCount == 2);
    checkControls();
    assert(tapCtl(S7, ui, d.fleet, Ctl::Net, 1, 8100) && joins == 1 && !strcmp(joinedSsid, "cafe") && !joinedPass[0]);
    assert(S7.shade().page == ShadePage::Main);

    // A network with a password: the keyboard, every printable character, then connect.
    assert(tapCtl(S7, ui, d.fleet, Ctl::Choose, -1, 9000));
    S7.draw(c, in);
    S7.draw(c, in);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Net, 0, 9100) && S7.shade().page == ShadePage::Password);
    checkControls();
    S7.draw(c, in);
    assert(guards());
    assert(tapCtl(S7, ui, d.fleet, Ctl::Connect, -1, 9200) && joins == 1);   // empty: nothing happens
    const char* pw = "Ab1 -/:;()$&@.,?!'\"_[]{}#%^*+=\\|~<>`";
    typeText(S7, ui, d.fleet, pw, 9300);
    assert(!strcmp(S7.shade().pass, pw));
    for (int i = 0; i < 2; i++) {
      checkControls();
      tapCtl(S7, ui, d.fleet, Ctl::Show, -1, 9400);
      S7.draw(c, in);
      assert(guards());
    }
    tapCtl(S7, ui, d.fleet, Ctl::Backspace, -1, 9500);
    assert(strlen(S7.shade().pass) == strlen(pw) - 1);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Connect, -1, 9600) && joins == 2 && !strcmp(joinedSsid, "lab"));
    assert(!strncmp(joinedPass, pw, strlen(pw) - 1));
    assert(S7.shade().page == ShadePage::Main && !S7.shade().pass[0]);  // the password isn't kept

    // A password at most 64 long.
    assert(tapCtl(S7, ui, d.fleet, Ctl::Choose, -1, 10000));
    S7.draw(c, in);
    S7.draw(c, in);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Net, 0, 10100));
    for (int i = 0; i < 70; i++) tapCtl(S7, ui, d.fleet, Ctl::Key, 'a', 10200);
    assert(strlen(S7.shade().pass) == 64);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Back, -1, 10300) && S7.shade().page == ShadePage::Networks);
    assert(!S7.shade().pass[0]);
    assert(tapCtl(S7, ui, d.fleet, Ctl::Back, -1, 10400) && S7.shade().page == ShadePage::Main);

    // Swipe up closes; the grid gets touches again.
    S7.touch(ui, d.fleet, true, 400, 400, DeckScope::Act, 11000);
    S7.touch(ui, d.fleet, true, 400, 200, DeckScope::Act, 11020);
    S7.touch(ui, d.fleet, false, 400, 200, DeckScope::Act, 11040);
    assert(S7.shade().page == ShadePage::Closed);

    // No deck: the grid ignores touch, the bar still opens the settings.
    in.link = LinkState::Lost;
    S7.draw(c, in);
    PanelUi ui2;
    const BigGrid g = bigGrid(S, bigPages(d.fleet));
    const Rect r = bigCell(g, 0);
    tapAt(S7, ui2, d.fleet, r.x + r.w / 2, r.y + r.h / 2 + StatusBar::kHeight, 12000);
    assert(ui2.screen == Screen::Home && !ui2.down);
    tapAt(S7, ui2, d.fleet, 400, 8, 12100);
    assert(S7.shade().page == ShadePage::Main);
    S7.draw(c, in);
    assert(guards());
  }

  // ---- what the Wi-Fi line says
  {
    char buf[96];
    ShadeWifi w;
    shadeWifiState(w, buf, sizeof buf);
    assert(strstr(buf, "idf5"));
    w.capable = true;
    shadeWifiState(w, buf, sizeof buf);
    assert(strstr(buf, "choose"));
    snprintf(w.ssid, sizeof w.ssid, "lab");
    w.via = NetVia::Wifi;
    w.phase = NetPhase::Online;
    snprintf(w.ip, sizeof w.ip, "192.0.2.7");
    w.rssi = -60;
    shadeWifiState(w, buf, sizeof buf);
    assert(strstr(buf, "connected") && strstr(buf, "192.0.2.7") && strstr(buf, "80%"));
    w.phase = NetPhase::WrongPassword;
    shadeWifiState(w, buf, sizeof buf);
    assert(!strcmp(buf, "wrong password"));
  }

  printf("ok  status bar\n");
  return 0;
}
