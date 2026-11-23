#include "reminder_picker.h"

#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {

constexpr wchar_t kClass[] = L"ESN.ReminderPicker";
constexpr int kIdDate = 100;
constexpr int kIdTime = 101;
constexpr int kIdOk = 102;
constexpr int kIdClear = 103;
constexpr int kDwmCornerRound = 2;

struct State {
  HWND owner = nullptr;
  HWND date = nullptr;
  HWND time = nullptr;
  HFONT font = nullptr;
  HFONT title_font = nullptr;
  Palette palette{};
  int dpi = 96;
  bool dark = false;
  bool done = false;
  bool ok = false;
  bool cleared = false;
  RECT close_rect{};
};

State* state_of(HWND hwnd) {
  return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

COLORREF to_ref(const D2D1_COLOR_F& c) {
  return RGB(BYTE(c.r * 255.0f + 0.5f), BYTE(c.g * 255.0f + 0.5f), BYTE(c.b * 255.0f + 0.5f));
}

long long systemtime_to_ms(const SYSTEMTIME& local) {
  FILETIME local_ft{}, utc{};
  if (!SystemTimeToFileTime(&local, &local_ft)) return 0;
  if (!LocalFileTimeToFileTime(&local_ft, &utc)) return 0;
  ULARGE_INTEGER v;
  v.LowPart = utc.dwLowDateTime;
  v.HighPart = utc.dwHighDateTime;
  return static_cast<long long>((v.QuadPart - 116444736000000000ULL) / 10000ULL);
}

void ms_to_systemtime(long long ms, SYSTEMTIME* local) {
  ULARGE_INTEGER v;
  v.QuadPart = static_cast<unsigned long long>(ms) * 10000ULL + 116444736000000000ULL;
  FILETIME utc{v.LowPart, v.HighPart}, local_ft{};
  FileTimeToLocalFileTime(&utc, &local_ft);
  FileTimeToSystemTime(&local_ft, local);
}

void finish(HWND hwnd, bool ok, bool cleared) {
  State* st = state_of(hwnd);
  if (!st || st->done) return;
  st->ok = ok;
  st->cleared = cleared;
  st->done = true;
  ShowWindow(hwnd, SW_HIDE);
}

int header_height(const State* st) { return MulDiv(38, st->dpi, 96); }

void paint(HWND hwnd, State* st) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  const int dpi = st->dpi;
  const int header = header_height(st);
  const COLORREF bg = to_ref(st->palette.bg);
  const COLORREF raised = to_ref(st->palette.raised);
  const COLORREF text = to_ref(st->palette.text);
  const COLORREF muted = to_ref(st->palette.muted);
  const COLORREF line = to_ref(st->palette.line);

  HBRUSH bg_brush = CreateSolidBrush(bg);
  FillRect(dc, &rc, bg_brush);
  DeleteObject(bg_brush);

  RECT header_rc{0, 0, rc.right, header};
  HBRUSH header_brush = CreateSolidBrush(raised);
  FillRect(dc, &header_rc, header_brush);
  DeleteObject(header_brush);

  HPEN line_pen = CreatePen(PS_SOLID, 1, line);
  HGDIOBJ old_pen = SelectObject(dc, line_pen);
  MoveToEx(dc, 0, header - 1, nullptr);
  LineTo(dc, rc.right, header - 1);
  SelectObject(dc, old_pen);
  DeleteObject(line_pen);

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, text);
  HGDIOBJ old_font = SelectObject(dc, st->title_font);
  RECT title_rc{MulDiv(16, dpi, 96), 0, rc.right, header};
  DrawTextW(dc, L"提醒时间", -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, old_font);

  // 右上角关闭按钮
  SetTextColor(dc, muted);
  RECT close = st->close_rect;
  HGDIOBJ old_font2 = SelectObject(dc, st->font);
  DrawTextW(dc, L"✕", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(dc, old_font2);

  // 外框
  HPEN border_pen = CreatePen(PS_SOLID, 1, line);
  HGDIOBJ old_border = SelectObject(dc, border_pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, 0, 0, rc.right, rc.bottom);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_border);
  DeleteObject(border_pen);

  EndPaint(hwnd, &ps);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  State* st = state_of(hwnd);
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return TRUE;
    }
    case WM_NCCALCSIZE:
      if (wp) return 0;
      break;
    case WM_NCHITTEST: {
      if (!st) break;
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);
      if (pt.y < header_height(st)) return HTCAPTION;  // 整条标题栏可拖动
      return HTCLIENT;
    }
    case WM_PAINT:
      if (st) paint(hwnd, st);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONDOWN: {
      if (!st) break;
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      if (PtInRect(&st->close_rect, pt)) {
        finish(hwnd, false, false);
        return 0;
      }
      break;
    }
    case WM_COMMAND:
      if (!st) break;
      if (LOWORD(wp) == kIdOk) {
        SYSTEMTIME date{};
        SYSTEMTIME time{};
        DateTime_GetSystemtime(st->date, &date);
        DateTime_GetSystemtime(st->time, &time);
        SYSTEMTIME local = date;
        local.wHour = time.wHour;
        local.wMinute = time.wMinute;
        local.wSecond = 0;
        local.wMilliseconds = 0;
        const long long out = systemtime_to_ms(local);
        SetPropW(hwnd, L"ESN.ReminderMs", reinterpret_cast<HANDLE>(out));
        finish(hwnd, true, false);
        return 0;
      }
      if (LOWORD(wp) == kIdClear) {
        finish(hwnd, true, true);
        return 0;
      }
      if (LOWORD(wp) == IDCANCEL) {
        finish(hwnd, false, false);
        return 0;
      }
      break;
    case WM_CLOSE:
      finish(hwnd, false, false);
      return 0;
    case WM_DESTROY:
      RemovePropW(hwnd, L"ESN.ReminderMs");
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool pick_reminder_time(HWND owner, const Palette& palette, long long initial_ms, long long* out_ms,
                        bool* cleared) {
  if (!out_ms || !cleared) return false;
  *cleared = false;

  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    registered = RegisterClassW(&wc) != 0;
    if (!registered) return false;
  }

  State state;
  state.owner = owner;
  state.palette = palette;
  state.dark = palette.dark;
  state.dpi = int(GetDpiForWindow(owner ? owner : GetDesktopWindow()));
  if (state.dpi <= 0) state.dpi = 96;
  const int dpi = state.dpi;
  const auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

  RECT anchor{0, 0, 0, 0};
  if (owner) GetWindowRect(owner, &anchor);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);
  const int width = S(360);
  const int height = S(208);
  int x = (anchor.left + anchor.right) / 2 - width / 2;
  int y = (anchor.top + anchor.bottom) / 2 - height / 2;
  x = std::clamp(x, int(mi.rcWork.left), int(mi.rcWork.right) - width);
  y = std::clamp(y, int(mi.rcWork.top), int(mi.rcWork.bottom) - height);

  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kClass, L"提醒时间",
                              WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner, nullptr,
                              GetModuleHandleW(nullptr), &state);
  if (!hwnd) return false;

  if (state.dark) {
    BOOL value = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &value, sizeof(value));
  }
  int corner = kDwmCornerRound;
  DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

  state.font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei UI");
  state.title_font = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                                 FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                 L"Microsoft YaHei UI");

  const int header = header_height(&state);
  state.close_rect = RECT{width - S(36), S(4), width - S(8), header - S(4)};

  state.date = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, S(18),
                               header + S(12), width - S(36), S(30), hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDate)),
                               GetModuleHandleW(nullptr), nullptr);
  SendMessageW(state.date, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"yyyy-MM-dd"));

  state.time = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT, S(18),
                               header + S(52), S(130), S(30), hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTime)),
                               GetModuleHandleW(nullptr), nullptr);
  SendMessageW(state.time, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH:mm"));

  const long long initial = initial_ms > 0 ? initial_ms : now_ms() + 3600LL * 1000;
  SYSTEMTIME st_local{};
  ms_to_systemtime(initial, &st_local);
  st_local.wSecond = 0;
  st_local.wMilliseconds = 0;
  SendMessageW(state.date, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&st_local));
  SendMessageW(state.time, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&st_local));

  const int buttons_y = height - S(48);
  HWND ok = CreateWindowExW(0, L"BUTTON", L"确定",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            width - S(184), buttons_y, S(76), S(30), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOk)),
                            GetModuleHandleW(nullptr), nullptr);
  HWND cancel = CreateWindowExW(0, L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                width - S(100), buttons_y, S(76), S(30), hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                                GetModuleHandleW(nullptr), nullptr);
  HWND clear = CreateWindowExW(0, L"BUTTON", L"清除提醒",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(18),
                               buttons_y, S(104), S(30), hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClear)),
                               GetModuleHandleW(nullptr), nullptr);

  for (HWND child : {state.date, state.time, ok, cancel, clear}) {
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    if (state.dark) SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
  }

  EnableWindow(owner, FALSE);
  ShowWindow(hwnd, SW_SHOW);
  SetFocus(state.date);

  bool quit = false;
  MSG msg{};
  while (!state.done && !quit) {
    const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
    if (got <= 0) {
      quit = true;
      break;
    }
    if (msg.message == WM_KEYDOWN) {
      if (msg.wParam == VK_RETURN) {
        SendMessageW(hwnd, WM_COMMAND, kIdOk, 0);
        continue;
      }
      if (msg.wParam == VK_ESCAPE) {
        finish(hwnd, false, false);
        continue;
      }
    }
    if (!IsDialogMessageW(hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (quit) PostQuitMessage(int(msg.wParam));

  const LONG_PTR stored = reinterpret_cast<LONG_PTR>(GetPropW(hwnd, L"ESN.ReminderMs"));
  DestroyWindow(hwnd);
  if (state.font) DeleteObject(state.font);
  if (state.title_font) DeleteObject(state.title_font);
  EnableWindow(owner, TRUE);
  if (owner) SetForegroundWindow(owner);

  if (!state.ok) return false;
  if (state.cleared) {
    *cleared = true;
    return true;
  }
  *out_ms = static_cast<long long>(stored);
  return *out_ms > 0;
}
