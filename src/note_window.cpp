#include "note_window.h"

#include <algorithm>
#include <commctrl.h>
#include <cwchar>
#include <cwctype>
#include <dwmapi.h>
#include <windowsx.h>

#include "app.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {

constexpr int kDwmCornerRound = 2;

std::wstring trim(std::wstring text) {
  size_t begin = 0;
  while (begin < text.size() && iswspace(text[begin])) ++begin;
  size_t end = text.size();
  while (end > begin && iswspace(text[end - 1])) --end;
  return text.substr(begin, end - begin);
}

std::wstring clamp_text(std::wstring text, size_t max_chars) {
  if (text.size() > max_chars) {
    text.resize(max_chars);
    if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();
  }
  return text;
}

}  // namespace

NoteWindow::NoteWindow(Note* note) : note_(note) {}

NoteWindow::~NoteWindow() {
  if (edit_brush_) DeleteObject(edit_brush_);
}

bool NoteWindow::create() {
  static bool class_registered = false;
  if (!class_registered) {
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc = s_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = reinterpret_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32,
                   LR_DEFAULTCOLOR));
    wc.lpszClassName = kClass;
    class_registered = RegisterClassW(&wc) != 0;
    if (!class_registered) {
      logf(L"[note] RegisterClass failed err=%lu", GetLastError());
      return false;
    }
  }
  RECT rc = note_->rect;
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, note_->name.c_str(),
                          WS_POPUP | WS_CLIPCHILDREN,
                          rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                          nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (!hwnd_) {
    logf(L"[note] CreateWindow failed err=%lu", GetLastError());
    return false;
  }
  ensure_visible();
  sync_topmost();
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  return true;
}

void NoteWindow::invalidate() {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::show(bool visible) {
  if (!hwnd_) return;
  if (visible) {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    sync_topmost();
  } else {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

void NoteWindow::refresh_theme() {
  if (edit_brush_) {
    DeleteObject(edit_brush_);
    edit_brush_ = nullptr;
  }
  cache_.clear();
  dark_frame_applied_ = false;
  sync_topmost();
  invalidate();
}

void NoteWindow::sync_topmost() {
  if (!hwnd_) return;
  BOOL dark = App::get().palette().dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  int corner = kDwmCornerRound;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
  SetWindowPos(hwnd_, note_->pinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  dark_frame_applied_ = true;
}

void NoteWindow::ensure_visible() {
  if (!hwnd_) return;
  RECT rc;
  GetWindowRect(hwnd_, &rc);
  HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{sizeof(mi)};
  if (!GetMonitorInfoW(mon, &mi)) return;
  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;
  int x = rc.left;
  int y = rc.top;
  const RECT& work = mi.rcWork;
  if (x + w > work.right) x = work.right - w;
  if (y + h > work.bottom) y = work.bottom - h;
  if (x < work.left) x = work.left;
  if (y < work.top) y = work.top;
  if (x != rc.left || y != rc.top) {
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void NoteWindow::begin_rename() {
  if (!hwnd_) return;
  ensure_edit();
  if (!edit_) return;
  if (edit_kind_ != EditKind::None) commit_edit(false);
  edit_kind_ = EditKind::Name;
  edit_item_ = -1;
  MoveWindow(edit_, pad(), S(4), width_ - 2 * pad(), header_h() - S(8), TRUE);
  SetWindowTextW(edit_, note_->name.c_str());
  ShowWindow(edit_, SW_SHOW);
  SetFocus(edit_);
  SendMessageW(edit_, EM_SETSEL, 0, -1);
}

LRESULT CALLBACK NoteWindow::s_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<NoteWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
  self->hwnd_ = hwnd;  // WM_NCCREATE 起就要可用
  return self->handle(msg, wp, lp);
}

LRESULT CALLBACK NoteWindow::s_edit_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR, DWORD_PTR ref) {
  auto* self = reinterpret_cast<NoteWindow*>(ref);
  switch (msg) {
    case WM_GETDLGCODE:
      if (wp == VK_RETURN || wp == VK_ESCAPE) return DLGC_WANTALLKEYS;
      break;
    case WM_KEYDOWN:
      if (wp == VK_RETURN) {
        self->commit_edit(true);
        return 0;
      }
      if (wp == VK_ESCAPE) {
        self->cancel_edit();
        return 0;
      }
      break;
    case WM_CHAR:
      if (wp == VK_RETURN || wp == VK_ESCAPE) return 0;
      break;
    case WM_KILLFOCUS:
      if (!self->committing_ && self->edit_kind_ != EditKind::None) self->commit_edit(false);
      break;
    default:
      break;
  }
  return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT NoteWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE:
      on_create();
      return 0;
    case WM_PAINT:
      on_paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      on_size(LOWORD(lp), HIWORD(lp));
      return 0;
    case WM_DPICHANGED:
      on_dpi_changed(HIWORD(wp), reinterpret_cast<const RECT*>(lp));
      return 0;

    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd_, &pt);
      if (edge_at(pt.x, pt.y) != 0) return HTCLIENT;  // 缩放自己处理，不走系统边框
      return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      mmi->ptMinTrackSize.x = S(240);
      mmi->ptMinTrackSize.y = S(180);
      return 0;
    }
    case WM_MOUSEMOVE:
      on_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_MOUSELEAVE:
      on_mouse_leave();
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd_);
      on_lbutton_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_LBUTTONUP:
      on_lbutton_up();
      return 0;
    case WM_LBUTTONDBLCLK:
      on_lbutton_dblclk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_RBUTTONUP:
      on_rbutton_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_MOUSEWHEEL:
      on_wheel(GET_WHEEL_DELTA_WPARAM(wp));
      return 0;
    case WM_KEYDOWN:
      on_key_down(wp);
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd_, &p);
        const int edge = edge_at(p.x, p.y);
        LPCWSTR id = IDC_ARROW;
        switch (edge) {
          case 1:
          case 2: id = IDC_SIZEWE; break;
          case 4:
          case 8: id = IDC_SIZENS; break;
          case 1 | 4:
          case 2 | 8: id = IDC_SIZENWSE; break;
          case 2 | 4:
          case 1 | 8: id = IDC_SIZENESW; break;
          default: break;
        }
        SetCursor(LoadCursorW(nullptr, id));
        return TRUE;
      }
      break;
    case WM_SETTINGCHANGE:
      if (App::get().skin() == Skin::System) {
        App::get().apply_theme();
      }
      return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      on_ctl_color_edit(reinterpret_cast<HDC>(wp));
      return reinterpret_cast<LRESULT>(edit_brush_);
    case WM_EXITSIZEMOVE: {
      RECT rc;
      GetWindowRect(hwnd_, &rc);
      note_->rect = rc;
      App::get().schedule_save();
      return 0;
    }
    case WM_SHOWWINDOW:
      if (!wp) {
        cache_.clear();
        discard_rt();
      }
      return 0;
    case WM_CLOSE:
      App::get().remove_note(this);
      return 0;
    case WM_DESTROY:
      discard_rt();
      hwnd_ = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

void NoteWindow::on_create() {
  dpi_ = int(GetDpiForWindow(hwnd_));
  if (dpi_ <= 0) dpi_ = 96;
  RECT rc;
  GetClientRect(hwnd_, &rc);
  width_ = rc.right;
  height_ = rc.bottom;

  if (App::get().d2d()->CreatePathGeometry(&pin_geo_) == S_OK) {
    ComPtr<ID2D1GeometrySink> sink;
    if (pin_geo_->Open(&sink) == S_OK) {
      sink->BeginFigure(D2D1::Point2F(-2.2f, -3.0f), D2D1_FIGURE_BEGIN_FILLED);
      sink->AddLine(D2D1::Point2F(2.2f, -3.0f));
      sink->AddLine(D2D1::Point2F(1.0f, 1.4f));
      sink->AddLine(D2D1::Point2F(0.5f, 1.4f));
      sink->AddLine(D2D1::Point2F(0.5f, 4.8f));
      sink->AddLine(D2D1::Point2F(0.0f, 5.6f));
      sink->AddLine(D2D1::Point2F(-0.5f, 4.8f));
      sink->AddLine(D2D1::Point2F(-0.5f, 1.4f));
      sink->AddLine(D2D1::Point2F(-1.0f, 1.4f));
      sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      sink->Close();
    }
  }
  stroke_round_.Reset();
  App::get().d2d()->CreateStrokeStyle(
      D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                  D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
      nullptr, 0, &stroke_round_);
  sync_topmost();
}

void NoteWindow::ensure_edit() {
  if (edit_) return;
  edit_ = CreateWindowExW(0, L"EDIT", L"",
                          WS_CHILD | ES_LEFT | ES_AUTOHSCROLL | ES_NOHIDESEL, 0, 0, 10, 10,
                          hwnd_, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
  if (!edit_) return;
  SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(App::get().formats(dpi_).font), TRUE);
  SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(2), S(2)));
  SetWindowSubclass(edit_, s_edit_proc, 1, reinterpret_cast<DWORD_PTR>(this));
}

void NoteWindow::on_size(int w, int h) {
  width_ = w;
  height_ = h;
  // DC 渲染目标无需 Resize：每次绘制都会 BindDC 到当前客户区
  if (edit_kind_ != EditKind::None) move_edit();
  clamp_scroll();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::on_dpi_changed(int dpi, const RECT* suggested) {
  if (dpi > 0) dpi_ = dpi;
  // 尺寸一律按物理像素自己管：不采用系统建议矩形，避免跨屏时被缩放两次
  (void)suggested;
  cache_.clear();
  if (edit_) {
    SendMessageW(edit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(App::get().formats(dpi_).font), TRUE);
  }
  if (edit_kind_ != EditKind::None) move_edit();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

int NoteWindow::S(int v) const { return MulDiv(v, dpi_, 96); }
int NoteWindow::header_h() const { return S(40); }
int NoteWindow::footer_h() const { return S(38); }
int NoteWindow::row_h() const { return S(32); }
int NoteWindow::pad() const { return S(14); }
int NoteWindow::list_top() const { return header_h(); }
int NoteWindow::list_bottom() const { return height_ - footer_h(); }

std::vector<int> NoteWindow::visible_items() const {
  std::vector<int> rows;
  rows.reserve(note_->items.size());
  const bool hide_done = App::get().checked_hidden();
  for (int i = 0; i < int(note_->items.size()); ++i) {
    if (hide_done && note_->items[size_t(i)].done) continue;
    rows.push_back(i);
  }
  return rows;
}

int NoteWindow::max_scroll() const {
  const int content = int(visible_items().size()) * row_h() + S(12);
  const int view = list_bottom() - list_top();
  return std::max(0, content - view);
}

void NoteWindow::clamp_scroll() { scroll_ = std::clamp(scroll_, 0, max_scroll()); }

RECT NoteWindow::item_rect(int visible_row) const {
  RECT r;
  r.left = pad();
  r.right = width_ - pad();
  r.top = list_top() + S(6) + visible_row * row_h() - scroll_;
  r.bottom = r.top + row_h();
  return r;
}

RECT NoteWindow::check_rect(int visible_row) const {
  RECT row = item_rect(visible_row);
  const int box = S(15);
  RECT r;
  r.left = row.left + S(2);
  r.top = row.top + (row_h() - box) / 2;
  r.right = r.left + box;
  r.bottom = r.top + box;
  return r;
}

NoteWindow::RowCache* NoteWindow::row_cache(const std::wstring& id) {
  for (auto& entry : cache_) {
    if (entry.first == id) return &entry.second;
  }
  cache_.emplace_back(id, RowCache{});
  return &cache_.back().second;
}

int NoteWindow::edge_at(int x, int y) const {
  const int b = S(6);
  int edge = 0;
  if (x < b) edge |= 1;
  if (x >= width_ - b) edge |= 2;
  if (y < b) edge |= 4;
  if (y >= height_ - b) edge |= 8;
  return edge;
}

RECT NoteWindow::clock_rect(int visible_row) const {
  RECT row = item_rect(visible_row);
  const int box = S(24);
  RECT r;
  r.right = row.right;
  r.left = r.right - box;
  r.top = row.top + (row_h() - box) / 2;
  r.bottom = r.top + box;
  return r;
}

RECT NoteWindow::header_button_rect(int index) const {
  const int box = S(24);
  RECT r;
  r.right = width_ - pad() - (1 - index) * S(28);  // 0 = 置顶（左）, 1 = 收起（右）
  r.left = r.right - box;
  r.top = (header_h() - box) / 2;
  r.bottom = r.top + box;
  return r;
}

int NoteWindow::item_at_y(int y) const {
  if (y < list_top() || y >= list_bottom()) return -1;
  const auto rows = visible_items();
  if (rows.empty()) return -1;
  const int index = (y - list_top() - S(6) + scroll_) / row_h();
  if (index < 0 || index >= int(rows.size())) return -1;
  return rows[size_t(index)];
}

bool NoteWindow::in_footer(int y) const { return y >= height_ - footer_h(); }

void NoteWindow::on_mouse_move(int x, int y) {
  if (resizing_) {
    on_resize_move(x, y);
    return;
  }
  if (dragging_) {
    POINT pt;
    GetCursorPos(&pt);
    SetWindowPos(hwnd_, nullptr, drag_window_.left + (pt.x - drag_origin_.x),
                 drag_window_.top + (pt.y - drag_origin_.y), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }
  if (!tracking_leave_) {
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&tme);
    tracking_leave_ = true;
  }
  (void)x;
  const int index = item_at_y(y);
  int row = -1;
  if (index >= 0) {
    const auto rows = visible_items();
    for (int i = 0; i < int(rows.size()); ++i) {
      if (rows[size_t(i)] == index) {
        row = i;
        break;
      }
    }
  }
  if (row != hover_row_) {
    hover_row_ = row;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void NoteWindow::on_mouse_leave() {
  tracking_leave_ = false;
  if (hover_row_ != -1) {
    hover_row_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void NoteWindow::on_lbutton_down(int x, int y) {
  const int edge = edge_at(x, y);
  if (edge != 0) {
    resizing_ = true;
    resize_edge_ = edge;
    GetCursorPos(&resize_origin_);
    GetWindowRect(hwnd_, &resize_start_);
    SetCapture(hwnd_);
    return;
  }
  if (y < header_h()) {
    POINT hp{x, y};
    RECT pin = header_button_rect(0);
    RECT hide = header_button_rect(1);
    if (PtInRect(&pin, hp)) {
      App::get().toggle_pin(this);
      return;
    }
    if (PtInRect(&hide, hp)) {
      App::get().hide_note(this);
      return;
    }
    dragging_ = true;
    SetCapture(hwnd_);
    POINT pt;
    GetCursorPos(&pt);
    drag_origin_ = pt;
    GetWindowRect(hwnd_, &drag_window_);
    return;
  }
  if (in_footer(y)) {
    add_item_and_edit(int(note_->items.size()) - 1);
    return;
  }
  const int index = item_at_y(y);
  if (index < 0) {
    if (visible_items().empty()) add_item_and_edit(-1);
    return;
  }
  const auto rows = visible_items();
  int row = 0;
  for (int i = 0; i < int(rows.size()); ++i) {
    if (rows[size_t(i)] == index) {
      row = i;
      break;
    }
  }
  RECT box = check_rect(row);
  RECT clock = clock_rect(row);
  POINT pt{x, y};
  selected_ = index;
  if (PtInRect(&clock, pt)) {
    POINT screen = pt;
    ClientToScreen(hwnd_, &screen);
    App::get().show_remind_menu(this, index, screen);
    return;
  }
  if (PtInRect(&box, pt)) {
    toggle_item(index);
  } else {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void NoteWindow::on_resize_move(int x, int y) {
  (void)x;
  (void)y;
  POINT pt;
  GetCursorPos(&pt);
  const int dx = pt.x - resize_origin_.x;
  const int dy = pt.y - resize_origin_.y;
  RECT r = resize_start_;
  const int min_w = S(240);
  const int min_h = S(180);

  if (resize_edge_ & 1) {
    r.left += dx;
    if (r.right - r.left < min_w) r.left = r.right - min_w;
  }
  if (resize_edge_ & 2) {
    r.right += dx;
    if (r.right - r.left < min_w) r.right = r.left + min_w;
  }
  if (resize_edge_ & 4) {
    r.top += dy;
    if (r.bottom - r.top < min_h) r.top = r.bottom - min_h;
  }
  if (resize_edge_ & 8) {
    r.bottom += dy;
    if (r.bottom - r.top < min_h) r.bottom = r.top + min_h;
  }
  SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void NoteWindow::on_lbutton_up() {
  if (resizing_) {
    resizing_ = false;
    ReleaseCapture();
    RECT rc;
    GetWindowRect(hwnd_, &rc);
    note_->rect = rc;
    App::get().schedule_save();
    return;
  }
  if (!dragging_) return;
  dragging_ = false;
  ReleaseCapture();
  RECT rc;
  GetWindowRect(hwnd_, &rc);
  note_->rect = rc;
  App::get().schedule_save();
}

void NoteWindow::on_lbutton_dblclk(int x, int y) {
  (void)x;
  if (y < header_h()) {
    begin_rename();
    return;
  }
  const int index = item_at_y(y);
  if (index >= 0) begin_edit_item(index);
}

void NoteWindow::on_rbutton_up(int x, int y) {
  POINT pt{x, y};
  ClientToScreen(hwnd_, &pt);
  const int index = item_at_y(y);
  if (index >= 0) {
    selected_ = index;
    App::get().show_item_menu(this, index, pt);
  } else {
    App::get().show_note_menu(this, pt);
  }
}

void NoteWindow::on_wheel(int delta) {
  scroll_ -= delta / WHEEL_DELTA * row_h() * 2;
  clamp_scroll();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::on_key_down(WPARAM key) {
  if (key == VK_F2) {
    begin_rename();
    return;
  }
  if (key == 'R' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) {
    if (selected_ >= 0 && selected_ < int(note_->items.size())) {
      App::get().open_custom_reminder(this, selected_);
    }
    return;
  }
  if (key == 'H' && (GetKeyState(VK_CONTROL) & 0x8000)) {
    App::get().hide_note(this);
    return;
  }
  if (key == VK_RETURN) {
    if (selected_ >= 0 && selected_ < int(note_->items.size())) begin_edit_item(selected_);
    else add_item_and_edit(int(note_->items.size()) - 1);
    return;
  }
  if (key == VK_DELETE && selected_ >= 0 && selected_ < int(note_->items.size())) {
    remove_item(selected_);
    return;
  }
  if (key == VK_SPACE && selected_ >= 0 && selected_ < int(note_->items.size())) {
    toggle_item(selected_);
    return;
  }
  if (key == VK_DOWN || key == VK_UP) {
    const auto rows = visible_items();
    if (rows.empty()) return;
    int pos = -1;
    for (int i = 0; i < int(rows.size()); ++i) {
      if (rows[size_t(i)] == selected_) pos = i;
    }
    int next = (key == VK_DOWN) ? pos + 1 : pos - 1;
    if (pos < 0) next = 0;
    next = std::clamp(next, 0, int(rows.size()) - 1);
    selected_ = rows[size_t(next)];
    scroll_into_view(selected_);
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void NoteWindow::on_ctl_color_edit(HDC dc) {
  const auto& p = App::get().palette();
  SetTextColor(dc, RGB(int(p.text.r * 255), int(p.text.g * 255), int(p.text.b * 255)));
  SetBkColor(dc, RGB(int(p.raised.r * 255), int(p.raised.g * 255), int(p.raised.b * 255)));
  if (!edit_brush_) {
    edit_brush_ = CreateSolidBrush(
        RGB(int(p.raised.r * 255), int(p.raised.g * 255), int(p.raised.b * 255)));
  }
}

void NoteWindow::ensure_rt() { App::get().render_target(); }

void NoteWindow::discard_rt() {}

ID2D1SolidColorBrush* NoteWindow::brush(int index) { return App::get().brush(index); }

void NoteWindow::on_paint() {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd_, &ps);
  ID2D1DCRenderTarget* rt = App::get().render_target();
  if (!rt) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    EndPaint(hwnd_, &ps);
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  HRESULT bind = rt->BindDC(dc, &client);  // 必须在 BeginDraw 之前
  rt->BeginDraw();
  rt->SetTransform(D2D1::Matrix3x2F::Identity());
  paint_card();
  HRESULT hr = rt->EndDraw();
  if (bind != S_OK || hr == D2DERR_RECREATE_TARGET) {
    if (hr == D2DERR_RECREATE_TARGET) App::get().discard_render_target();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  EndPaint(hwnd_, &ps);
}

void NoteWindow::paint_card() {
  ID2D1DCRenderTarget* rt = App::get().render_target();
  if (!rt) return;
  rt->Clear(App::get().palette().bg);
  paint_header(rt);
  paint_rows(rt);
  paint_footer(rt);

  const float inset = 0.5f;
  D2D1_ROUNDED_RECT border{
      D2D1::RectF(inset, inset, float(width_) - inset, float(height_) - inset),
      float(S(12)), float(S(12))};
  rt->DrawRoundedRectangle(border, brush(5), 1.0f);
}

static bool skip_text() {
  static const bool skip = GetEnvironmentVariableW(L"ESN_SKIP_TEXT", nullptr, 0) > 0;
  return skip;
}

void NoteWindow::paint_header(ID2D1RenderTarget* rt) {
  const int hh = header_h();
  rt->FillRectangle(D2D1::RectF(0, 0, float(width_), float(hh)), brush(1));
  rt->DrawLine(D2D1::Point2F(0, float(hh) - 0.5f),
               D2D1::Point2F(float(width_), float(hh) - 0.5f), brush(5), 1.0f);

  const float stroke_w = 1.6f * float(dpi_) / 96.0f;
  // 置顶按钮：斜置图钉（矢量，不依赖 emoji 字体），置顶时用强调色
  {
    RECT pr = header_button_rect(0);
    const float cx = float(pr.left + pr.right) / 2.0f;
    const float cy = float(pr.top + pr.bottom) / 2.0f;
    const float k = float(S(12)) / 12.0f;  // 图钉本体约 12px
    ID2D1SolidColorBrush* b = brush(note_->pinned ? 6 : 3);
    rt->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                     D2D1::Matrix3x2F::Translation(cx, cy));
    rt->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(-3.8f, -6.0f, 3.8f, -2.8f), 1.1f, 1.1f), b);
    if (pin_geo_) rt->FillGeometry(pin_geo_.Get(), b);
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
  }
  // 收起按钮
  {
    RECT hr = header_button_rect(1);
    const float cx = float(hr.left + hr.right) / 2.0f;
    const float cy = float(hr.top + hr.bottom) / 2.0f;
    rt->DrawLine(D2D1::Point2F(cx - float(S(6)), cy), D2D1::Point2F(cx + float(S(6)), cy),
                 brush(3), stroke_w);
  }

  const auto& fmts = App::get().formats(dpi_);
  if (!fmts.title || edit_kind_ == EditKind::Name || skip_text()) return;
  ComPtr<IDWriteTextLayout> layout;
  const std::wstring title = note_->name.empty() ? L"便签" : note_->name;
  if (SUCCEEDED(App::get().dwrite()->CreateTextLayout(
          title.c_str(), UINT32(title.size()), fmts.title.Get(),
          float(width_ - 2 * pad() - S(60)), float(hh), &layout))) {
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt->DrawTextLayout(D2D1::Point2F(float(pad()), 0), layout.Get(), brush(2));
  }
}

void NoteWindow::paint_rows(ID2D1RenderTarget* rt) {
  const int top = list_top();
  const int bottom = list_bottom();
  const auto rows = visible_items();
  const auto& fmts = App::get().formats(dpi_);
  if (rows.empty() || skip_text()) {
    if (!fmts.body) return;
    ComPtr<IDWriteTextLayout> layout;
    const wchar_t* hint = L"写下一件事，回车确认";
    if (SUCCEEDED(App::get().dwrite()->CreateTextLayout(
            hint, UINT32(wcslen(hint)), fmts.body.Get(), float(width_ - 2 * pad()),
            float(bottom - top), &layout))) {
      layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
      layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
      rt->DrawTextLayout(D2D1::Point2F(float(pad()), float(top)), layout.Get(), brush(3));
    }
    return;
  }

  rt->PushAxisAlignedClip(D2D1::RectF(0, float(top), float(width_), float(bottom)),
                          D2D1_ANTIALIAS_MODE_ALIASED);
  const bool hide_done = App::get().checked_hidden();

  for (int row = 0; row < int(rows.size()); ++row) {
    const int index = rows[size_t(row)];
    const Item& item = note_->items[size_t(index)];
    RECT box = item_rect(row);
    if (box.bottom < top || box.top > bottom) continue;

    if (row == hover_row_ || index == selected_) {
      D2D1_ROUNDED_RECT rr{D2D1::RectF(float(box.left - S(4)), float(box.top + S(2)),
                                       float(box.right + S(4)), float(box.bottom - S(2))),
                           float(S(6)), float(S(6))};
      rt->FillRoundedRectangle(rr, brush(8));
    }

    RECT cr = check_rect(row);
    D2D1_ROUNDED_RECT rr{
        D2D1::RectF(float(cr.left), float(cr.top), float(cr.right), float(cr.bottom)),
        float(S(4)), float(S(4))};
    if (item.done) {
      rt->FillRoundedRectangle(rr, brush(6));
      const float s = float(cr.right - cr.left);
      const float x = float(cr.left);
      const float y = float(cr.top);
      const float w = float(S(2));
      rt->DrawLine(D2D1::Point2F(x + s * 0.26f, y + s * 0.54f),
                   D2D1::Point2F(x + s * 0.44f, y + s * 0.72f), brush(9), w, stroke_round_.Get());
      rt->DrawLine(D2D1::Point2F(x + s * 0.44f, y + s * 0.72f),
                   D2D1::Point2F(x + s * 0.76f, y + s * 0.30f), brush(9), w, stroke_round_.Get());
    } else {
      rt->DrawRoundedRectangle(rr, brush(5), 1.4f);
    }

    if (!fmts.body) continue;
    const float indent = float(S(14) * item.indent);
    const float text_left = float(cr.right) + S(10) + indent;
    const float badge = float(S(30)) + (item.remind > 0 ? float(S(44)) : 0.0f);
    const float text_width = float(box.right) - text_left - badge;
    if (text_width <= 4.0f) continue;

    RowCache& cache = *row_cache(item.id);
    if (!cache.layout || cache.text != item.text || cache.done != item.done ||
        cache.width != text_width) {
      cache.text = item.text;
      cache.done = item.done;
      cache.width = text_width;
      cache.layout.Reset();
      if (!item.text.empty()) {
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(App::get().dwrite()->CreateTextLayout(
                item.text.c_str(), UINT32(item.text.size()), fmts.body.Get(), text_width,
                float(row_h()), &layout))) {
          layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
          if (item.done && !hide_done) {
            DWRITE_TEXT_RANGE range{0, UINT32(item.text.size())};
            layout->SetStrikethrough(TRUE, range);
          }
          cache.layout = std::move(layout);
        }
      }
    }
    if (cache.layout) {
      rt->DrawTextLayout(D2D1::Point2F(text_left, float(box.top)),
                         cache.layout.Get(), brush(item.done ? 4 : 2));
    }
    if (fmts.small_fmt) {
      RECT cr2 = clock_rect(row);
      const float ccx = float(cr2.left + cr2.right) / 2.0f;
      const float ccy = float(cr2.top + cr2.bottom) / 2.0f;
      const float radius = float(S(6));
      const bool overdue = item.remind > 0 && item.remind <= now_ms();
      ID2D1SolidColorBrush* cb = brush(overdue ? 7 : (item.remind > 0 ? 6 : 3));
      const float clock_w = 1.3f * float(dpi_) / 96.0f;
      rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ccx, ccy), radius, radius), cb, clock_w);
      rt->DrawLine(D2D1::Point2F(ccx, ccy), D2D1::Point2F(ccx, ccy - radius * 0.55f), cb,
                   clock_w, stroke_round_.Get());
      rt->DrawLine(D2D1::Point2F(ccx, ccy),
                   D2D1::Point2F(ccx + radius * 0.5f, ccy + radius * 0.25f), cb, clock_w,
                   stroke_round_.Get());

      if (item.remind > 0) {
        const std::wstring clock = format_clock(item.remind);
        ComPtr<IDWriteTextLayout> badge_layout;
        if (SUCCEEDED(App::get().dwrite()->CreateTextLayout(
                clock.c_str(), UINT32(clock.size()), fmts.small_fmt.Get(), float(S(44)),
                float(row_h()), &badge_layout))) {
          badge_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
          badge_layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
          rt->DrawTextLayout(D2D1::Point2F(float(cr2.left - S(46)), float(box.top)),
                             badge_layout.Get(), cb);
        }
      }
    }
  }
  rt->PopAxisAlignedClip();

  // 限制 layout 缓存：滚动过大量条目后，把不在视口内的布局丢掉，避免缓存无上限增长
  if (cache_.size() > 16) {
    for (auto it = cache_.begin(); it != cache_.end();) {
      bool keep = false;
      for (int index : rows) {
        if (note_->items[size_t(index)].id == it->first) {
          keep = true;
          break;
        }
      }
      it = keep ? std::next(it) : cache_.erase(it);
    }
  }
}

void NoteWindow::paint_footer(ID2D1RenderTarget* rt) {
  const int top = height_ - footer_h();
  rt->DrawLine(D2D1::Point2F(0, float(top) + 0.5f),
               D2D1::Point2F(float(width_), float(top) + 0.5f), brush(5), 1.0f);

  if (!App::get().formats(dpi_).body) return;
  ComPtr<IDWriteTextLayout> layout;
  if (skip_text()) return;
  const wchar_t* label = L"+ 添加待办";
  if (SUCCEEDED(App::get().dwrite()->CreateTextLayout(
          label, UINT32(wcslen(label)), App::get().formats(dpi_).body.Get(), float(width_ - 2 * pad()),
          float(footer_h()), &layout))) {
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt->DrawTextLayout(D2D1::Point2F(float(pad()), float(top)), layout.Get(), brush(3));
  }

  int done = 0;
  for (const auto& item : note_->items) done += item.done ? 1 : 0;
  if (!App::get().formats(dpi_).small_fmt || done == 0) return;
  std::wstring count = L"已完成 " + format_uint(done);
  ComPtr<IDWriteTextLayout> counter;
  if (SUCCEEDED(App::get().dwrite()->CreateTextLayout(
          count.c_str(), UINT32(count.size()), App::get().formats(dpi_).small_fmt.Get(), float(width_ - 2 * pad()),
          float(footer_h()), &counter))) {
    counter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    counter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt->DrawTextLayout(D2D1::Point2F(float(pad()), float(top)), counter.Get(), brush(3));
  }
}

void NoteWindow::toggle_item(int item_index) {
  if (item_index < 0 || item_index >= int(note_->items.size())) return;
  auto& item = note_->items[size_t(item_index)];
  item.done = !item.done;
  if (item.done) item.remind = 0;  // 完成即取消提醒
  selected_ = item_index;
  clamp_scroll();
  App::get().schedule_save();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::remove_item(int item_index) {
  if (item_index < 0 || item_index >= int(note_->items.size())) return;
  note_->items.erase(note_->items.begin() + item_index);
  selected_ = std::min(selected_, int(note_->items.size()) - 1);
  clamp_scroll();
  App::get().schedule_save();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::delete_item(int item_index) { remove_item(item_index); }

void NoteWindow::add_item_and_edit(int after_item) {
  if (int(note_->items.size()) >= Store::kMaxItems) {
    MessageBeep(MB_ICONWARNING);
    return;
  }
  Item item;
  item.id = App::get().store().new_id(L"i");
  const int pos = std::clamp(after_item + 1, 0, int(note_->items.size()));
  note_->items.insert(note_->items.begin() + pos, std::move(item));
  begin_edit_item(pos);
}

void NoteWindow::begin_edit_item(int item_index) {
  if (item_index < 0 || item_index >= int(note_->items.size())) return;
  ensure_edit();
  if (!edit_) return;
  if (edit_kind_ != EditKind::None) commit_edit(false);

  edit_kind_ = EditKind::Item;
  edit_item_ = item_index;
  scroll_into_view(item_index);
  move_edit();
  SetWindowTextW(edit_, note_->items[size_t(item_index)].text.c_str());
  ShowWindow(edit_, SW_SHOW);
  SetFocus(edit_);
  SendMessageW(edit_, EM_SETSEL, 0, -1);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void NoteWindow::move_edit() {
  if (!edit_) return;
  if (edit_kind_ == EditKind::Name) {
    MoveWindow(edit_, pad(), S(4), width_ - 2 * pad(), header_h() - S(8), TRUE);
    return;
  }
  const auto rows = visible_items();
  int visible = -1;
  for (int i = 0; i < int(rows.size()); ++i) {
    if (rows[size_t(i)] == edit_item_) {
      visible = i;
      break;
    }
  }
  if (visible < 0) visible = 0;
  RECT box = item_rect(visible);
  RECT cr = check_rect(visible);
  const int indent = edit_item_ >= 0 && edit_item_ < int(note_->items.size())
                         ? note_->items[size_t(edit_item_)].indent
                         : 0;
  MoveWindow(edit_, int(cr.right) + S(10) + S(14) * indent, box.top + S(3),
             box.right - cr.right - S(10) - S(14) * indent - S(30), row_h() - S(6), TRUE);
}

void NoteWindow::commit_edit(bool chain_new) {
  if (edit_kind_ == EditKind::None) return;
  committing_ = true;

  wchar_t buffer[2048]{};
  GetWindowTextW(edit_, buffer, 2048);
  std::wstring text = trim(buffer);
  const EditKind kind = edit_kind_;
  const int index = edit_item_;
  end_edit();

  int next_edit = -1;
  if (kind == EditKind::Name) {
    if (!text.empty()) note_->name = clamp_text(text, Store::kMaxNameChars);
  } else if (kind == EditKind::Item) {
    const bool existed = index >= 0 && index < int(note_->items.size());
    if (text.empty()) {
      if (existed) note_->items.erase(note_->items.begin() + index);
    } else if (existed) {
      note_->items[size_t(index)].text = clamp_text(text, Store::kMaxItemChars);
      if (chain_new && int(note_->items.size()) < Store::kMaxItems) {
        Item item;
        item.id = App::get().store().new_id(L"i");
        note_->items.insert(note_->items.begin() + index + 1, std::move(item));
        next_edit = index + 1;
      }
    }
  }

  App::get().schedule_save();
  clamp_scroll();
  InvalidateRect(hwnd_, nullptr, FALSE);
  committing_ = false;
  SetFocus(hwnd_);
  if (next_edit >= 0) begin_edit_item(next_edit);
}

void NoteWindow::cancel_edit() {
  if (edit_kind_ == EditKind::None) return;
  const EditKind kind = edit_kind_;
  const int index = edit_item_;
  end_edit();
  if (kind == EditKind::Item && index >= 0 && index < int(note_->items.size()) &&
      note_->items[size_t(index)].text.empty()) {
    note_->items.erase(note_->items.begin() + index);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
  SetFocus(hwnd_);
}

void NoteWindow::end_edit() {
  edit_kind_ = EditKind::None;
  edit_item_ = -1;
  if (edit_) ShowWindow(edit_, SW_HIDE);
}

void NoteWindow::scroll_into_view(int item_index) {
  const auto rows = visible_items();
  int visible = -1;
  for (int i = 0; i < int(rows.size()); ++i) {
    if (rows[size_t(i)] == item_index) {
      visible = i;
      break;
    }
  }
  if (visible < 0) return;
  const int view = list_bottom() - list_top();
  const int top = S(6) + visible * row_h();
  if (top - scroll_ < 0) scroll_ = top;
  if (top + row_h() - scroll_ > view) scroll_ = top + row_h() - view;
  clamp_scroll();
}
