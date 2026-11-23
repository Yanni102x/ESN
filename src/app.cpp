#include "app.h"

#include <algorithm>
#include <commctrl.h>
#include <unordered_map>
#include <dwmapi.h>
#include <shellapi.h>

#include "note_window.h"
#include "reminder_picker.h"

namespace {
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kMsgRemoveNote = WM_APP + 7;
constexpr UINT_PTR kSaveTimer = 1;
constexpr UINT_PTR kRemindTimer = 2;
constexpr UINT_PTR kTopmostTimer = 3;
}  // namespace

App& App::get() {
  static App app;
  return app;
}

Skin App::skin() const { return skin_from_key(store_.settings.skin); }

bool App::init(HINSTANCE hinst) {
  hinst_ = hinst;

  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2d_.ReleaseAndGetAddressOf());
  logf(L"[app] d2d factory hr=0x%08lx", hr);
  if (FAILED(hr)) return false;
  hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                           reinterpret_cast<IUnknown**>(dwrite_.ReleaseAndGetAddressOf()));
  logf(L"[app] dwrite factory hr=0x%08lx", hr);
  if (FAILED(hr)) return false;

  WNDCLASSW wc{};
  wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
    App& app = App::get();
    if (!app.main_) app.main_ = hwnd;  // WM_NCCREATE 期间就需要有效句柄
    return app.handle(msg, wp, lp);
  };
  wc.hInstance = hinst_;
  wc.lpszClassName = kMainClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&wc)) {
    logf(L"[app] RegisterClass failed err=%lu", GetLastError());
    return false;
  }

  main_ = CreateWindowExW(0, kMainClass, L"ESN", WS_OVERLAPPED, 0, 0, 0, 0,
                          nullptr, nullptr, hinst_, nullptr);
  logf(L"[app] main window=%p err=%lu", main_, GetLastError());
  if (!main_) return false;

  msg_activate_ = RegisterWindowMessageW(L"ESN.Activate");
  msg_taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

  const bool parsed = store_.load();
  // 开机启动以注册表为准（用户可能在系统设置里改过），并顺手修正 exe 路径
  store_.settings.autostart = read_autostart();
  if (store_.settings.autostart) apply_autostart(true);
  logf(L"[app] load parsed=%d notes=%zu", parsed ? 1 : 0, store_.notes.size());
  apply_theme();

  const int cx = GetSystemMetrics(SM_CXSMICON);
  const int cy = GetSystemMetrics(SM_CYSMICON);
  icon_small_ = reinterpret_cast<HICON>(
      LoadImageW(hinst_, MAKEINTRESOURCEW(101), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
  icon_big_ = reinterpret_cast<HICON>(
      LoadImageW(hinst_, MAKEINTRESOURCEW(101), IMAGE_ICON, cx * 2, cy * 2, LR_DEFAULTCOLOR));

  add_tray_icon();
  RegisterHotKey(main_, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N');
  RegisterHotKey(main_, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'H');

  for (auto& note : store_.notes) {
    auto window = std::make_unique<NoteWindow>(note.get());
    if (window->create()) {
      windows_.push_back(std::move(window));
    } else {
        logf(L"[app] create note window failed for %s", note->name.c_str());
    }
  }
  if (windows_.empty()) new_note();
  reschedule_reminders();
  logf(L"[app] init done notes=%zu windows=%zu private=%llu KiB", store_.notes.size(),
       windows_.size(), private_bytes() / 1024);
  return true;
}

int App::run() {
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  save_now();
  remove_tray_icon();
  UnregisterHotKey(main_, 1);
  UnregisterHotKey(main_, 2);
  return int(msg.wParam);
}

void App::apply_theme() {
  Skin effective = skin();
  if (effective == Skin::System) effective = system_uses_light() ? Skin::Light : Skin::Dark;
  palette_ = palette_for(effective);
  discard_render_target();  // 画刷与调色板绑定，一并重建
  for (auto& window : windows_) window->refresh_theme();
}

TextFormats& App::formats(int dpi) {
  auto it = formats_.find(dpi);
  if (it != formats_.end()) return *it->second;

  auto fmts = std::make_unique<TextFormats>();
  auto make = [&](float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>* out) {
    if (SUCCEEDED(dwrite_->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            size * dpi / 96.0f, L"zh-CN",
                                            out->ReleaseAndGetAddressOf()))) {
      (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
      ComPtr<IDWriteInlineObject> sign;
      if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(out->Get(), &sign))) {
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        (*out)->SetTrimming(&trim, sign.Get());
      }
    }
  };
  make(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &fmts->title);
  make(14.0f, DWRITE_FONT_WEIGHT_NORMAL, &fmts->body);
  make(12.0f, DWRITE_FONT_WEIGHT_NORMAL, &fmts->small_fmt);
  fmts->font = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei UI");

  TextFormats& ref = *fmts;
  formats_[dpi] = std::move(fmts);
  return ref;
}

ID2D1DCRenderTarget* App::render_target() {
  if (rt_) return rt_.Get();
  D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
  HRESULT hr = d2d_->CreateDCRenderTarget(&props, &rt_);
  if (FAILED(hr)) {
    logf(L"[app] CreateDCRenderTarget failed hr=0x%08lx", hr);
    rt_.Reset();
    return nullptr;
  }
  logf(L"[app] shared dc render target ready private=%llu KiB", private_bytes() / 1024);
  return rt_.Get();
}

ID2D1SolidColorBrush* App::brush(int index) {
  if (!render_target()) return nullptr;
  if (!brushes_[index]) {
    D2D1_COLOR_F color = palette_.bg;
    switch (index) {
      case 0: color = palette_.bg; break;
      case 1: color = palette_.raised; break;
      case 2: color = palette_.text; break;
      case 3: color = palette_.muted; break;
      case 4: color = palette_.done; break;
      case 5: color = palette_.line; break;
      case 6: color = palette_.accent; break;
      case 7: color = palette_.danger; break;
      case 8: color = palette_.line; color.a = 0.45f; break;
      case 9: color = palette_.bg; break;
      default: break;
    }
    rt_->CreateSolidColorBrush(color, &brushes_[index]);
  }
  return brushes_[index].Get();
}

void App::discard_render_target() {
  for (auto& b : brushes_) b.Reset();
  rt_.Reset();
}

void App::set_skin(Skin skin) {
  store_.settings.skin = skin_key(skin);
  apply_theme();
  schedule_save();
}

void App::set_checked_hidden(bool hidden) {
  store_.settings.checked_hidden = hidden;
  for (auto& window : windows_) window->invalidate();
  schedule_save();
}

std::wstring App::exe_path() {
  wchar_t buffer[MAX_PATH * 2]{};
  DWORD n = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
  return std::wstring(buffer, n);
}

bool App::read_autostart() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    LR"(Software\Microsoft\Windows\CurrentVersion\Run)", 0,
                    KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t data[MAX_PATH * 2]{};
  DWORD size = sizeof(data);
  DWORD type = 0;
  const bool exists = RegQueryValueExW(key, L"ESN", nullptr, &type,
                                       reinterpret_cast<LPBYTE>(data), &size) == ERROR_SUCCESS;
  RegCloseKey(key);
  return exists;
}

void App::apply_autostart(bool enable) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER,
                      LR"(Software\Microsoft\Windows\CurrentVersion\Run)", 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
    MessageBoxW(main_, L"写入开机启动项失败（HKCU 不可写）。", L"ESN", MB_OK | MB_ICONWARNING);
    return;
  }
  if (enable) {
    const std::wstring value = L"\"" + exe_path() + L"\"";
    RegSetValueExW(key, L"ESN", 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   DWORD((value.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, L"ESN");
  }
  RegCloseKey(key);
  if (store_.settings.autostart != enable) {
    store_.settings.autostart = enable;
    schedule_save();
  }
}

void App::schedule_save() {
  if (main_) SetTimer(main_, kSaveTimer, 600, nullptr);
  reschedule_reminders();
}

void App::save_now() {
  if (main_) KillTimer(main_, kSaveTimer);
  store_.save();
}

void App::new_note() {
  if (int(store_.notes.size()) >= Store::kMaxNotes) {
    MessageBoxW(main_, L"最多同时保留 12 张便签。", L"ESN", MB_OK | MB_ICONINFORMATION);
    return;
  }
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int w = 340;
  const int h = 440;
  const int step = int(store_.notes.size()) * 28 % 220;

  auto note = std::make_unique<Note>();
  note->id = store_.new_id(L"n");
  note->name = L"便签 " + format_uint(store_.notes.size() + 1);
  const int left = int(work.left);
  const int top = int(work.top);
  const int right = int(work.right);
  const int bottom = int(work.bottom);
  int x = right - w - 60 - step;
  int y = top + 60 + step;
  x = std::clamp(x, left, std::max(left, right - w));
  y = std::clamp(y, top, std::max(top, bottom - h));
  note->rect = RECT{x, y, x + w, y + h};

  Note* raw = note.get();
  store_.notes.push_back(std::move(note));
  auto window = std::make_unique<NoteWindow>(raw);
  if (window->create()) {
    windows_.push_back(std::move(window));
  }
  schedule_save();
}

void App::show_all() {
  for (auto& window : windows_) window->show(true);
}

void App::show_all_and_focus() {
  show_all();
  for (auto& window : windows_) {
    if (window->hwnd() && IsWindowVisible(window->hwnd())) {
      SetForegroundWindow(window->hwnd());
      break;
    }
  }
}

void App::hide_all() {
  for (auto& window : windows_) window->show(false);
}

void App::remove_note(NoteWindow* window) {
  if (!window) return;
  std::wstring message = L"删除便签「" + window->note().name + L"」及其全部待办？";
  if (MessageBoxW(window->hwnd(), message.c_str(), L"ESN",
                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
    return;
  }
  window->show(false);
  PostMessageW(main_, kMsgRemoveNote, 0, reinterpret_cast<LPARAM>(window));
}

void App::hide_note(NoteWindow* window) {
  if (window) window->show(false);
}

void App::toggle_pin(NoteWindow* window) {
  if (!window) return;
  window->note().pinned = !window->note().pinned;
  window->sync_topmost();
  window->invalidate();
  schedule_save();
}

void App::reschedule_reminders() {
  if (!main_) return;
  KillTimer(main_, kRemindTimer);
  long long next = 0;
  for (const auto& note : store_.notes) {
    for (const auto& item : note->items) {
      if (item.done || item.remind <= 0) continue;
      if (next == 0 || item.remind < next) next = item.remind;
    }
  }
  if (next == 0) return;
  long long delay = next - now_ms();
  if (delay < 200) delay = 200;
  if (delay > 6LL * 3600 * 1000) delay = 6LL * 3600 * 1000;  // 最多睡 6 小时，防止跨睡眠漂移
  SetTimer(main_, kRemindTimer, UINT(delay), nullptr);
}

void App::notify(const std::wstring& title, const std::wstring& text) {
  if (!tray_added_) return;
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = main_;
  data.uID = kTrayId;
  data.uFlags = NIF_INFO;
  data.dwInfoFlags = NIIF_INFO;
  data.uTimeout = 10000;
  wcsncpy_s(data.szInfoTitle, std::size(data.szInfoTitle), title.c_str(), _TRUNCATE);
  wcsncpy_s(data.szInfo, std::size(data.szInfo), text.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

void App::bring_to_front(NoteWindow* window) {
  if (!window || !window->hwnd()) return;
  window->show(true);
  SetWindowPos(window->hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetForegroundWindow(window->hwnd());
  FLASHWINFO flash{sizeof(flash), window->hwnd(), FLASHW_ALL | FLASHW_TIMERNOFG, 4, 0};
  FlashWindowEx(&flash);
  SetTimer(main_, kTopmostTimer, 15000, nullptr);
}

void App::fire_due_reminders() {
  const long long now = now_ms();
  bool changed = false;
  for (auto& window : windows_) {
    auto& note = window->note();
    std::vector<std::wstring> due;
    for (auto& item : note.items) {
      if (item.done || item.remind <= 0 || item.remind > now + 300) continue;
      item.remind = 0;
      due.push_back(item.text.empty() ? L"(空条目)" : item.text);
      changed = true;
    }
    if (due.empty()) continue;
    bring_to_front(window.get());
    std::wstring text = L"「" + (note.name.empty() ? std::wstring(L"便签") : note.name) + L"」";
    text += due.front();
    if (due.size() > 1) text += L" 等 " + format_uint(due.size()) + L" 项";
    notify(L"ESN", text);
    window->invalidate();
  }
  if (changed) save_now();
  reschedule_reminders();
}

void App::set_item_remind(long long when_ms) {
  if (!context_note_ || context_item_ < 0) return;
  auto& items = context_note_->note().items;
  if (context_item_ >= int(items.size())) return;
  auto& target = items[size_t(context_item_)];
  target.remind = when_ms;
  if (when_ms > 0) target.done = false;  // 设提醒意味着这件事还要做
  context_note_->invalidate();
  schedule_save();
}

void App::show_note_menu(NoteWindow* window, POINT point) {
  if (!window) return;
  context_note_ = window;
  HMENU menu = build_note_menu(window);
  SetForegroundWindow(main_);
  const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                 point.x, point.y, 0, main_, nullptr);
  DestroyMenu(menu);
  if (cmd) handle_command(cmd);
  context_note_ = nullptr;
  PostMessageW(main_, WM_NULL, 0, 0);
}

void App::add_skin_items(HMENU menu) {
  AppendMenuW(menu, MF_STRING, kCmdSkinPaper, L"宣纸");
  AppendMenuW(menu, MF_STRING, kCmdSkinLight, L"浅色");
  AppendMenuW(menu, MF_STRING, kCmdSkinDark, L"深色");
  AppendMenuW(menu, MF_STRING, kCmdSkinSystem, L"跟随系统");
  int checked = kCmdSkinPaper;
  switch (skin()) {
    case Skin::Light: checked = kCmdSkinLight; break;
    case Skin::Dark: checked = kCmdSkinDark; break;
    case Skin::System: checked = kCmdSkinSystem; break;
    default: checked = kCmdSkinPaper; break;
  }
  CheckMenuRadioItem(menu, kCmdSkinLight, kCmdSkinSystem, checked, MF_BYCOMMAND);
}

HMENU App::build_note_menu(NoteWindow* window) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kCmdNoteRename, L"重命名\tF2");
  AppendMenuW(menu, MF_STRING | (window->note().pinned ? MF_CHECKED : 0), kCmdNotePin, L"置顶");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

  HMENU skin_menu = CreatePopupMenu();
  add_skin_items(skin_menu);
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(skin_menu), L"皮肤");

  HMENU check_menu = CreatePopupMenu();
  AppendMenuW(check_menu, MF_STRING, kCmdCheckStrike, L"显示删除线");
  AppendMenuW(check_menu, MF_STRING, kCmdCheckHide, L"隐藏已完成");
  CheckMenuRadioItem(check_menu, kCmdCheckStrike, kCmdCheckHide,
                     store_.settings.checked_hidden ? kCmdCheckHide : kCmdCheckStrike,
                     MF_BYCOMMAND);
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(check_menu), L"完成项显示");

  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdNoteHide, L"隐藏到托盘	Ctrl+H");
  AppendMenuW(menu, MF_STRING, kCmdNoteDelete, L"删除便签");
  return menu;
}

HMENU App::build_remind_menu(NoteWindow* window, int item_index) {
  HMENU menu = CreatePopupMenu();
  const auto& items = window->note().items;
  if (item_index >= 0 && item_index < int(items.size()) &&
      items[size_t(item_index)].remind > 0) {
    std::wstring label = L"当前：" + format_clock(items[size_t(item_index)].remind);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label.c_str());
    AppendMenuW(menu, MF_STRING, kCmdItemRemindClear, L"清除提醒");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }
  AppendMenuW(menu, MF_STRING, kCmdItemRemind10, L"10 分钟后");
  AppendMenuW(menu, MF_STRING, kCmdItemRemind30, L"30 分钟后");
  AppendMenuW(menu, MF_STRING, kCmdItemRemind60, L"1 小时后");
  AppendMenuW(menu, MF_STRING, kCmdItemRemindTomorrow, L"明天 9:00");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdItemRemindCustom, L"自定义…");
  return menu;
}

void App::open_custom_reminder(NoteWindow* window, int item_index) {
  if (!window || item_index < 0) return;
  auto& items = window->note().items;
  if (item_index >= int(items.size())) return;
  long long picked = 0;
  bool cleared = false;
  if (!pick_reminder_time(window->hwnd(), palette_, items[size_t(item_index)].remind, &picked,
                          &cleared)) {
    return;
  }
  context_note_ = window;
  context_item_ = item_index;
  set_item_remind(cleared ? 0 : picked);
  context_note_ = nullptr;
  context_item_ = -1;
}

void App::show_remind_menu(NoteWindow* window, int item_index, POINT point) {
  if (!window) return;
  context_note_ = window;
  context_item_ = item_index;
  HMENU menu = build_remind_menu(window, item_index);
  SetForegroundWindow(main_);
  const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                 point.x, point.y, 0, main_, nullptr);
  DestroyMenu(menu);
  if (cmd) handle_command(cmd);
  context_note_ = nullptr;
  context_item_ = -1;
  PostMessageW(main_, WM_NULL, 0, 0);
}

HMENU App::build_item_menu(NoteWindow* window, int item_index) {
  HMENU menu = CreatePopupMenu();
  const auto& items = window->note().items;
  const bool has = item_index >= 0 && item_index < int(items.size());
  if (has && items[size_t(item_index)].remind > 0) {
    std::wstring label = L"提醒：" + format_clock(items[size_t(item_index)].remind);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label.c_str());
    AppendMenuW(menu, MF_STRING, kCmdItemRemindClear, L"清除提醒");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }
  HMENU when = CreatePopupMenu();
  AppendMenuW(when, MF_STRING, kCmdItemRemind10, L"10 分钟后");
  AppendMenuW(when, MF_STRING, kCmdItemRemind30, L"30 分钟后");
  AppendMenuW(when, MF_STRING, kCmdItemRemind60, L"1 小时后");
  AppendMenuW(when, MF_STRING, kCmdItemRemindTomorrow, L"明天 9:00");
  AppendMenuW(when, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(when, MF_STRING, kCmdItemRemindCustom, L"自定义…");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(when), L"提醒我");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdItemDelete, L"删除这一条");
  return menu;
}

void App::show_item_menu(NoteWindow* window, int item_index, POINT point) {
  if (!window) return;
  context_note_ = window;
  context_item_ = item_index;
  HMENU menu = build_item_menu(window, item_index);
  SetForegroundWindow(main_);
  const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                 point.x, point.y, 0, main_, nullptr);
  DestroyMenu(menu);
  if (cmd) handle_command(cmd);
  context_note_ = nullptr;
  context_item_ = -1;
  PostMessageW(main_, WM_NULL, 0, 0);
}

void App::handle_command(int id) {
  switch (id) {
    case kCmdNewNote: new_note(); break;
    case kCmdShowAll: show_all(); break;
    case kCmdHideAll: hide_all(); break;
    case kCmdSkinLight: set_skin(Skin::Light); break;
    case kCmdSkinDark: set_skin(Skin::Dark); break;
    case kCmdSkinPaper: set_skin(Skin::Paper); break;
    case kCmdSkinSystem: set_skin(Skin::System); break;
    case kCmdCheckStrike: set_checked_hidden(false); break;
    case kCmdCheckHide: set_checked_hidden(true); break;
    case kCmdAutostart: apply_autostart(!store_.settings.autostart); break;
    case kCmdNoteRename:
      if (context_note_) context_note_->begin_rename();
      break;
    case kCmdNotePin:
      toggle_pin(context_note_);
      break;
    case kCmdNoteDelete:
      if (context_note_) remove_note(context_note_);
      break;
    case kCmdNoteHide:
      if (context_note_) hide_note(context_note_);
      break;
    case kCmdItemRemind10:
      set_item_remind(now_ms() + 10LL * 60 * 1000);
      break;
    case kCmdItemRemind30:
      set_item_remind(now_ms() + 30LL * 60 * 1000);
      break;
    case kCmdItemRemind60:
      set_item_remind(now_ms() + 60LL * 60 * 1000);
      break;
    case kCmdItemRemindTomorrow:
      set_item_remind(tomorrow_nine_ms());
      break;
    case kCmdItemRemindClear:
      set_item_remind(0);
      break;
    case kCmdItemRemindCustom:
      open_custom_reminder(context_note_, context_item_);
      break;
    case kCmdItemDelete:
      if (context_note_ && context_item_ >= 0) {
        context_note_->delete_item(context_item_);
      }
      break;
    case kCmdExit:
      PostQuitMessage(0);
      break;
    default:
      break;
  }
}

void App::on_hotkey(int id) {
  if (id == 1) {
    new_note();
    return;
  }
  if (id == 2) {
    bool any_visible = false;
    for (auto& window : windows_) {
      if (window->hwnd() && IsWindowVisible(window->hwnd())) any_visible = true;
    }
    if (any_visible) hide_all();
    else show_all();
  }
}

void App::add_tray_icon() {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = main_;
  data.uID = kTrayId;
  data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data.uCallbackMessage = kTrayMessage;
  data.hIcon = icon_small_;
  wcscpy_s(data.szTip, std::size(data.szTip), L"ESN");
  tray_added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
}

void App::remove_tray_icon() {
  if (!tray_added_) return;
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = main_;
  data.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &data);
  tray_added_ = false;
}


void App::on_tray_event(UINT event) {
  switch (event) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU: {
      POINT pt;
      GetCursorPos(&pt);
      HMENU menu = CreatePopupMenu();
      AppendMenuW(menu, MF_STRING, kCmdNewNote, L"新建便签");
      AppendMenuW(menu, MF_STRING, kCmdShowAll, L"显示全部");
      AppendMenuW(menu, MF_STRING, kCmdHideAll, L"隐藏全部");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      HMENU skin_menu = CreatePopupMenu();
      add_skin_items(skin_menu);
      AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(skin_menu), L"皮肤");
      HMENU check_menu = CreatePopupMenu();
      AppendMenuW(check_menu, MF_STRING, kCmdCheckStrike, L"显示删除线");
      AppendMenuW(check_menu, MF_STRING, kCmdCheckHide, L"隐藏已完成");
      CheckMenuRadioItem(check_menu, kCmdCheckStrike, kCmdCheckHide,
                         store_.settings.checked_hidden ? kCmdCheckHide : kCmdCheckStrike,
                         MF_BYCOMMAND);
      AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(check_menu), L"完成项显示");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING | (store_.settings.autostart ? MF_CHECKED : 0), kCmdAutostart,
                  L"开机启动");
      AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");
      SetForegroundWindow(main_);
      const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, main_, nullptr);
      DestroyMenu(menu);
      if (cmd) handle_command(cmd);
      PostMessageW(main_, WM_NULL, 0, 0);
      break;
    }
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
      show_all_and_focus();
      break;
    default:
      break;
  }
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kTrayMessage) {
    on_tray_event(UINT(lp));
    return 0;
  }
  if (msg == msg_activate_ && msg_activate_) {
    show_all();
    return 0;
  }
  if (msg == msg_taskbar_created_ && msg_taskbar_created_) {
    tray_added_ = false;
    add_tray_icon();
    return 0;
  }
  switch (msg) {
    case WM_HOTKEY:
      on_hotkey(int(wp));
      return 0;
    case WM_TIMER:
      if (wp == kSaveTimer) {
        KillTimer(main_, kSaveTimer);
        store_.save();
      } else if (wp == kRemindTimer) {
        KillTimer(main_, kRemindTimer);
        fire_due_reminders();
      } else if (wp == kTopmostTimer) {
        KillTimer(main_, kTopmostTimer);
        for (auto& window : windows_) {
          if (!window->note().pinned && window->hwnd()) {
            SetWindowPos(window->hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
          }
        }
      }
      return 0;
    case kMsgRemoveNote: {
      auto* target = reinterpret_cast<NoteWindow*>(lp);
      if (target) {
        std::wstring id = target->note().id;
        if (target->hwnd()) DestroyWindow(target->hwnd());
        store_.notes.erase(
            std::remove_if(store_.notes.begin(), store_.notes.end(),
                           [&](const std::unique_ptr<Note>& note) { return note->id == id; }),
            store_.notes.end());
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                           [&](const std::unique_ptr<NoteWindow>& w) { return w.get() == target; }),
            windows_.end());
        schedule_save();
      }
      return 0;
    }
    case WM_COMMAND:
      handle_command(LOWORD(wp));
      return 0;
    case WM_SETTINGCHANGE:
      if (skin() == Skin::System) apply_theme();
      return 0;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wp) save_now();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(main_, msg, wp, lp);
}
