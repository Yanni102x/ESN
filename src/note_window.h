#pragma once

#include "common.h"
#include "store.h"

class NoteWindow {
 public:
  static constexpr wchar_t kClass[] = L"ESN.Note";

  explicit NoteWindow(Note* note);
  ~NoteWindow();

  bool create();
  HWND hwnd() const { return hwnd_; }
  Note& note() { return *note_; }
  const Note& note() const { return *note_; }

  void invalidate();
  void begin_rename();
  void sync_topmost();
  void show(bool visible);
  void ensure_visible();
  void refresh_theme();
  void delete_item(int item_index);

 private:
  enum class EditKind { None, Item, Name };

  struct RowCache {
    std::wstring text;
    bool done = false;
    float width = 0;
    ComPtr<IDWriteTextLayout> layout;
  };

  static LRESULT CALLBACK s_wnd_proc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK s_edit_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
  void on_create();
  void on_paint();
  void on_size(int w, int h);
  void on_dpi_changed(int dpi, const RECT* suggested);
  void on_mouse_move(int x, int y);
  void on_mouse_leave();
  void on_lbutton_down(int x, int y);
  void on_lbutton_up();
  void on_lbutton_dblclk(int x, int y);
  void on_rbutton_up(int x, int y);
  void on_wheel(int delta);
  void on_key_down(WPARAM key);
  void on_ctl_color_edit(HDC dc);

  void ensure_edit();
  void ensure_rt();
  void discard_rt();
  void paint_card();
  void paint_header(ID2D1RenderTarget* rt);
  void paint_rows(ID2D1RenderTarget* rt);
  void paint_footer(ID2D1RenderTarget* rt);
  ID2D1SolidColorBrush* brush(int index);

  int S(int v) const;
  int header_h() const;
  int footer_h() const;
  int row_h() const;
  int pad() const;
  int list_top() const;
  int list_bottom() const;

  std::vector<int> visible_items() const;
  int item_at_y(int y) const;
  int edge_at(int x, int y) const;
  void on_resize_move(int x, int y);
  RECT item_rect(int visible_row) const;
  RECT check_rect(int visible_row) const;
  RECT clock_rect(int visible_row) const;
  RECT header_button_rect(int index) const;  // 0 = 置顶, 1 = 收起
  RowCache* row_cache(const std::wstring& id);
  bool in_footer(int y) const;
  int max_scroll() const;
  void clamp_scroll();
  void scroll_into_view(int item_index);

  struct UndoOp {
    enum class Kind { Toggle, EditText, Delete, Add, Move, Rename } kind = Kind::Toggle;
    std::wstring item_id;
    int index = 0;
    int to_index = 0;
    std::wstring before;
    std::wstring after;
    bool before_done = false;
    bool after_done = false;
    Item removed;
  };
  void push_undo(UndoOp op);
  void undo();
  void redo();
  int item_index_of(const std::wstring& id) const;
  void move_item(int from, int to);
  int drop_index_at(int y) const;

  void toggle_item(int item_index);
  void remove_item(int item_index);
  void add_item_and_edit(int after_item);
  void begin_edit_item(int item_index);
  void move_edit();
  void commit_edit(bool chain_new);
  void cancel_edit();
  void end_edit();

  Note* note_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND edit_ = nullptr;
  HBRUSH edit_brush_ = nullptr;

  int dpi_ = 96;
  int width_ = 0;
  int height_ = 0;
  int scroll_ = 0;
  int hover_row_ = -1;
  int selected_ = -1;
  bool tracking_leave_ = false;
  bool dragging_ = false;
  POINT drag_origin_{};
  RECT drag_window_{};
  int drag_item_ = -1;
  int drag_start_y_ = 0;
  bool drag_active_ = false;
  int drop_index_ = -1;
  int drop_y_ = 0;
  std::vector<UndoOp> undo_, redo_;
  bool edit_new_item_ = false;
  bool resizing_ = false;
  int resize_edge_ = 0;
  RECT resize_start_{};
  POINT resize_origin_{};
  bool committing_ = false;

  EditKind edit_kind_ = EditKind::None;
  int edit_item_ = -1;

  ComPtr<ID2D1StrokeStyle> stroke_round_;
  ComPtr<ID2D1PathGeometry> pin_geo_;
  std::vector<std::pair<std::wstring, RowCache>> cache_;
  bool dark_frame_applied_ = false;
};
