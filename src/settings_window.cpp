#include "settings_window.h"

#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "app.h"
#include "store.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {

constexpr wchar_t kClass[] = L"ESN.Settings";
constexpr int kIdSkinLight = 200;
constexpr int kIdSkinDark = 201;
constexpr int kIdSkinPaper = 202;
constexpr int kIdSkinSystem = 203;
constexpr int kIdCheckStrike = 210;
constexpr int kIdCheckHide = 211;
constexpr int kIdAutostart = 220;
constexpr int kIdOpenDir = 230;
constexpr int kIdClose = 231;
constexpr int kDwmCornerRound = 2;

struct State {
  HWND owner = nullptr;
  HFONT font = nullptr;
  HFONT title_font = nullptr;
  int dpi = 96;
  RECT close_rect{};
  HBRUSH bg_brush = nullptr;
};

HWND g_window = nullptr;

State* state_of(HWND hwnd) {
  return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

COLORREF to_ref(const D2D1_COLOR_F& c) {
  return RGB(BYTE(c.r * 255.0f + 0.5f), BYTE(c.g * 255.0f + 0.5f), BYTE(c.b * 255.0f + 0.5f));
}

int header_h(const State* st) { return MulDiv(44, st->dpi, 96); }

void paint(HWND hwnd, State* st) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  const auto& palette = App::get().palette();
  const int header = header_h(st);

  HBRUSH b = CreateSolidBrush(to_ref(palette.bg));
  FillRect(dc, &rc, b);
  DeleteObject(b);
  RECT hr{0, 0, rc.right, header};
  b = CreateSolidBrush(to_ref(palette.raised));
  FillRect(dc, &hr, b);
  DeleteObject(b);

  HPEN pen = CreatePen(PS_SOLID, 1, to_ref(palette.line));
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, 0, header - 1, nullptr);
  LineTo(dc, rc.right, header - 1);
  SelectObject(dc, old);
  DeleteObject(pen);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, to_ref(palette.text));
  HGDIOBJ old_font = SelectObject(dc, st->title_font);
  RECT tr{MulDiv(18, st->dpi, 96), 0, rc.right, header};
  DrawTextW(dc, L"设置", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, old_font);

  SetTextColor(dc, to_ref(palette.muted));
  old_font = SelectObject(dc, st->font);
  DrawTextW(dc, L"✕", -1, &st->close_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, old_font);

  pen = CreatePen(PS_SOLID, 1, to_ref(palette.line));
  old = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, 0, 0, rc.right, rc.bottom);
  SelectObject(dc, old_brush);
  SelectObject(dc, old);
  DeleteObject(pen);

  EndPaint(hwnd, &ps);
}

void apply_dark_theme(HWND hwnd, bool dark) {
  BOOL value = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &value, sizeof(value));
  for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
    SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
  }
}

void sync_controls(HWND hwnd) {
  auto& store = App::get().store();
  int id = kIdSkinPaper;
  switch (App::get().skin()) {
    case Skin::Light: id = kIdSkinLight; break;
    case Skin::Dark: id = kIdSkinDark; break;
    case Skin::Paper: id = kIdSkinPaper; break;
    case Skin::System: id = kIdSkinSystem; break;
  }
  CheckRadioButton(hwnd, kIdSkinLight, kIdSkinSystem, id);
  CheckRadioButton(hwnd, kIdCheckStrike, kIdCheckHide,
                   store.settings.checked_hidden ? kIdCheckHide : kIdCheckStrike);
  SendDlgItemMessageW(hwnd, kIdAutostart, BM_SETCHECK,
                      store.settings.autostart ? BST_CHECKED : BST_UNCHECKED, 0);
}

void create_controls(HWND hwnd, State* st) {
  const int dpi = st->dpi;
  const auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
  const int width = S(460);
  const int header = 44;  // 逻辑单位，add() 里统一乘 DPI

  st->font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  st->title_font = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                               L"Microsoft YaHei UI");
  st->close_rect = RECT{width - S(40), S(6), width - S(10), S(header) - S(6)};
  st->bg_brush = CreateSolidBrush(to_ref(App::get().palette().bg));

  auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w,
                 int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                             hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             GetModuleHandleW(nullptr), nullptr);
    if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    return c;
  };

  add(L"STATIC", L"外观", SS_LEFT, 20, 56, 120, 20, 0);
  add(L"BUTTON", L"浅色", BS_AUTORADIOBUTTON | WS_GROUP, 20, 80, 96, 24, kIdSkinLight);
  add(L"BUTTON", L"深色", BS_AUTORADIOBUTTON, 124, 80, 96, 24, kIdSkinDark);
  add(L"BUTTON", L"宣纸", BS_AUTORADIOBUTTON, 20, 108, 96, 24, kIdSkinPaper);
  add(L"BUTTON", L"跟随系统", BS_AUTORADIOBUTTON, 124, 108, 140, 24, kIdSkinSystem);

  add(L"STATIC", L"完成项显示", SS_LEFT, 20, 146, 160, 20, 0);
  add(L"BUTTON", L"显示删除线", BS_AUTORADIOBUTTON | WS_GROUP, 20, 170, 128, 24, kIdCheckStrike);
  add(L"BUTTON", L"隐藏已完成", BS_AUTORADIOBUTTON, 152, 170, 128, 24, kIdCheckHide);

  add(L"BUTTON", L"开机启动", BS_AUTOCHECKBOX | WS_GROUP, 20, 206, 160, 24, kIdAutostart);

  add(L"STATIC", L"数据", SS_LEFT, 20, 244, 120, 20, 0);
  add(L"BUTTON", L"打开数据目录", BS_PUSHBUTTON | WS_GROUP, 20, 266, 148, 32, kIdOpenDir);

  add(L"STATIC", L"ESN 1.0.0 · Electronic Sticky Note", SS_LEFT, 20, 316, 300, 20, 0);
  add(L"BUTTON", L"关闭", BS_PUSHBUTTON, 344, 312, 96, 32, kIdClose);

  apply_dark_theme(hwnd, App::get().palette().dark);
  sync_controls(hwnd);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  State* st = state_of(hwnd);
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      auto* state = new State();
      *state = *reinterpret_cast<State*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
      return TRUE;
    }
    case WM_NCHITTEST: {
      if (!st) break;
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      if (pt.y < header_h(st)) return HTCAPTION;
      return HTCLIENT;
    }
    case WM_PAINT:
      if (st) paint(hwnd, st);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      if (!st) break;
      HDC dc = reinterpret_cast<HDC>(wp);
      const auto& palette = App::get().palette();
      SetTextColor(dc, to_ref(palette.text));
      SetBkColor(dc, to_ref(palette.bg));
      return reinterpret_cast<LRESULT>(st->bg_brush);
    }
    case WM_LBUTTONDOWN: {
      if (!st) break;
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      if (PtInRect(&st->close_rect, pt)) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
      }
      break;
    }
    case WM_COMMAND: {
      if (!st) break;
      const int id = LOWORD(wp);
      auto& app = App::get();
      switch (id) {
        case kIdSkinLight: app.set_skin(Skin::Light); break;
        case kIdSkinDark: app.set_skin(Skin::Dark); break;
        case kIdSkinPaper: app.set_skin(Skin::Paper); break;
        case kIdSkinSystem: app.set_skin(Skin::System); break;
        case kIdCheckStrike: app.set_checked_hidden(false); break;
        case kIdCheckHide: app.set_checked_hidden(true); break;
        case kIdAutostart:
          app.apply_autostart(SendDlgItemMessageW(hwnd, kIdAutostart, BM_GETCHECK, 0, 0) ==
                              BST_CHECKED);
          sync_controls(hwnd);
          break;
        case kIdOpenDir:
          ShellExecuteW(hwnd, L"open", app.store().data_dir().c_str(), nullptr, nullptr,
                        SW_SHOWNORMAL);
          break;
        case kIdClose:
          ShowWindow(hwnd, SW_HIDE);
          break;
        default:
          break;
      }
      if (id >= kIdSkinLight && id <= kIdSkinSystem) {
        apply_dark_theme(hwnd, app.palette().dark);
      }
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }
    case WM_CLOSE:
      ShowWindow(hwnd, SW_HIDE);
      return 0;
    case WM_DESTROY:
      if (st) {
        if (st->font) DeleteObject(st->font);
        if (st->title_font) DeleteObject(st->title_font);
        if (st->bg_brush) DeleteObject(st->bg_brush);
        delete st;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      }
      g_window = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void show_settings(HWND owner) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    registered = RegisterClassW(&wc) != 0;
    if (!registered) return;
  }

  if (g_window && IsWindow(g_window)) {
    sync_controls(g_window);
    ShowWindow(g_window, SW_SHOW);
    SetForegroundWindow(g_window);
    return;
  }

  State init;
  init.owner = owner;
  init.dpi = int(GetDpiForWindow(owner ? owner : GetDesktopWindow()));
  if (init.dpi <= 0) init.dpi = 96;
  const auto S = [&](int v) { return MulDiv(v, init.dpi, 96); };

  RECT anchor{};
  if (owner) GetWindowRect(owner, &anchor);
  if (anchor.right - anchor.left <= 0) {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &anchor, 0);  // 无可用便签时，落在工作区中心
  }
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);
  const int w = S(460);
  const int h = S(372);
  int x = (anchor.left + anchor.right) / 2 - w / 2;
  int y = (anchor.top + anchor.bottom) / 2 - h / 2;
  x = std::clamp(x, int(mi.rcWork.left), int(mi.rcWork.right) - w);
  y = std::clamp(y, int(mi.rcWork.top), int(mi.rcWork.bottom) - h);

  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kClass, L"ESN 设置",
                              WS_POPUP | WS_CLIPCHILDREN, x, y, w, h, owner, nullptr,
                              GetModuleHandleW(nullptr), &init);
  if (!hwnd) return;
  g_window = hwnd;

  int corner = kDwmCornerRound;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

  State* st = state_of(hwnd);
  if (st) create_controls(hwnd, st);
  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);
}
