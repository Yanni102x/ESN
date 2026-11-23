#pragma once

#include "common.h"
#include "store.h"

class NoteWindow;

enum : int {
  kCmdNewNote = 3001,
  kCmdShowAll,
  kCmdHideAll,

  kCmdSkinLight = 3010,
  kCmdSkinDark,
  kCmdSkinPaper,
  kCmdSkinSystem,

  kCmdCheckStrike = 3020,
  kCmdCheckHide,

  kCmdAutostart = 3030,
  kCmdSettings = 3031,

  kCmdNoteRename = 3101,
  kCmdNotePin,
  kCmdNoteDelete,
  kCmdNoteHide,

  kCmdItemRemind10 = 4010,
  kCmdItemRemind30,
  kCmdItemRemind60,
  kCmdItemRemindTomorrow,
  kCmdItemRemindClear,
  kCmdItemRemindCustom,
  kCmdItemDelete,

  kCmdExit = 3201,
};

struct TextFormats {
  ComPtr<IDWriteTextFormat> title;
  ComPtr<IDWriteTextFormat> body;
  ComPtr<IDWriteTextFormat> small_fmt;  // small 是 Windows 宏，不能用
  HFONT font = nullptr;  // 编辑框用
};

class App {
 public:
  static constexpr wchar_t kMainClass[] = L"ESN.Main";
  static constexpr wchar_t kNoteClass[] = L"ESN.Note";

  static App& get();

  bool init(HINSTANCE hinst);
  int run();

  ID2D1Factory* d2d() const { return d2d_.Get(); }
  IDWriteFactory* dwrite() const { return dwrite_.Get(); }
  TextFormats& formats(int dpi);          // 按 DPI 共享的文本格式与字体
  ID2D1DCRenderTarget* render_target();   // 全局共享的软件渲染目标
  ID2D1SolidColorBrush* brush(int index);
  void discard_render_target();
  Store& store() { return store_; }
  const Palette& palette() const { return palette_; }
  Skin skin() const;
  bool checked_hidden() const { return store_.settings.checked_hidden; }
  HWND main_wnd() const { return main_; }

  void set_skin(Skin skin);
  void set_checked_hidden(bool hidden);
  void apply_autostart(bool enable);
  void schedule_save();
  void save_now();

  void new_note();
  void show_all();
  void show_all_and_focus();
  void hide_all();
  void remove_note(NoteWindow* window);
  void hide_note(NoteWindow* window);
  void toggle_pin(NoteWindow* window);
  void show_note_menu(NoteWindow* window, POINT point);
  void show_remind_menu(NoteWindow* window, int item_index, POINT point);
  void open_custom_reminder(NoteWindow* window, int item_index);
  void show_item_menu(NoteWindow* window, int item_index, POINT point);
  void bring_to_front(NoteWindow* window);
  void reschedule_reminders();
  void apply_theme();

 private:
  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
  void on_tray_event(UINT event);
  void on_hotkey(int id);
  void handle_command(int id);
  HMENU build_note_menu(NoteWindow* window);
  HMENU build_item_menu(NoteWindow* window, int item_index);
  HMENU build_remind_menu(NoteWindow* window, int item_index);
  bool read_autostart();
  std::wstring exe_path();
  void fire_due_reminders();
  void notify(const std::wstring& title, const std::wstring& text);
  void set_item_remind(long long when_ms);
  void add_skin_items(HMENU menu);
  void add_tray_icon();
  void remove_tray_icon();

  HINSTANCE hinst_{};
  HWND main_{};
  HICON icon_small_{};
  HICON icon_big_{};
  ComPtr<ID2D1Factory> d2d_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1DCRenderTarget> rt_;
  std::unordered_map<int, std::unique_ptr<TextFormats>> formats_;
  ComPtr<ID2D1SolidColorBrush> brushes_[10];
  Store store_;
  Palette palette_{};
  std::vector<std::unique_ptr<NoteWindow>> windows_;
  NoteWindow* context_note_ = nullptr;
  int context_item_ = -1;
  bool tray_added_ = false;
  UINT msg_activate_ = 0;
  UINT msg_taskbar_created_ = 0;
};
