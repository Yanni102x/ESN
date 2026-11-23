#pragma once

#include "common.h"

struct Item {
  std::wstring id;
  std::wstring text;
  bool done = false;
  int indent = 0;
  long long remind = 0;  // Unix 毫秒；0 = 无提醒
};

struct Note {
  std::wstring id;
  std::wstring name;
  RECT rect{0, 0, 320, 420};
  bool pinned = false;
  std::vector<Item> items;
};

struct Settings {
  std::wstring skin = L"paper";
  bool checked_hidden = false;  // false = 删除线，true = 隐藏
  bool autostart = false;
};

class Store {
 public:
  static constexpr int kMaxNotes = 12;
  static constexpr int kMaxItems = 200;
  static constexpr int kMaxItemChars = 512;
  static constexpr int kMaxNameChars = 48;

  bool load();
  bool save();
  std::wstring new_id(const wchar_t* prefix);
  void seed_first_run();
  std::wstring data_dir() const { return dir_; }

  Settings settings;
  std::vector<std::unique_ptr<Note>> notes;

 private:
  std::wstring path() const;
  std::wstring backup_path() const;
  std::wstring temp_path() const;
  bool parse(const char* json, size_t size);

  std::wstring dir_;
  unsigned long long counter_ = 1;
};
