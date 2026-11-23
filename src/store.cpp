#include "store.h"

#include <shlobj.h>

#include <algorithm>

#include "yyjson.h"

namespace {

constexpr int kSchema = 1;

std::wstring local_app_data() {
  PWSTR raw = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) {
    out = raw;
    CoTaskMemFree(raw);
  }
  if (out.empty()) {
    wchar_t buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) out = buf;
  }
  return out;
}

bool read_file(const std::wstring& path, std::string* out) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > (32 << 20)) {
    CloseHandle(file);
    return false;
  }
  out->resize(size_t(size.QuadPart));
  DWORD read = 0;
  BOOL ok = ReadFile(file, out->data(), DWORD(out->size()), &read, nullptr);
  CloseHandle(file);
  if (!ok || read != out->size()) return false;
  return true;
}

bool write_file_atomic(const std::wstring& path, const std::wstring& tmp, const char* data,
                       size_t size) {
  HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(file, data, DWORD(size), &written, nullptr);
  if (ok) ok = FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok || written != size) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  if (!ReplaceFileW(path.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                    nullptr, nullptr)) {
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      DeleteFileW(tmp.c_str());
      return false;
    }
  }
  return true;
}

std::wstring get_str(yyjson_val* obj, const char* key, const wchar_t* fallback = L"") {
  yyjson_val* val = yyjson_obj_get(obj, key);
  if (!val || !yyjson_is_str(val)) return fallback;
  return widen(yyjson_get_str(val));
}

bool get_bool(yyjson_val* obj, const char* key, bool fallback = false) {
  yyjson_val* val = yyjson_obj_get(obj, key);
  if (!val || !yyjson_is_bool(val)) return fallback;
  return yyjson_get_bool(val);
}

long long get_i64(yyjson_val* obj, const char* key, long long fallback = 0) {
  yyjson_val* val = yyjson_obj_get(obj, key);
  if (!val || !yyjson_is_int(val)) return fallback;
  return yyjson_get_sint(val);
}

int get_int(yyjson_val* obj, const char* key, int fallback = 0) {
  yyjson_val* val = yyjson_obj_get(obj, key);
  if (!val || !yyjson_is_int(val)) return fallback;
  return int(yyjson_get_int(val));
}

std::wstring clamp_text(std::wstring text, size_t max_chars) {
  if (text.size() > max_chars) {
    text.resize(max_chars);
    // 避免截断在代理对中间
    if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();
  }
  return text;
}

}  // namespace

std::wstring Store::path() const { return dir_ + L"\\data.json"; }
std::wstring Store::backup_path() const { return dir_ + L"\\data.json.bak"; }
std::wstring Store::temp_path() const { return dir_ + L"\\data.json.tmp"; }

void Store::seed_first_run() {
  auto note = std::make_unique<Note>();
  note->id = new_id(L"n");
  note->name = L"今天";
  const int dpi = int(GetDpiForSystem());
  const int w = MulDiv(340, dpi, 96);
  const int h = MulDiv(440, dpi, 96);
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int x = std::max(int(work.left), int(work.right) - w - MulDiv(80, dpi, 96));
  const int y = std::max(int(work.top), int(work.top) + MulDiv(80, dpi, 96));
  note->rect = RECT{x, y, x + w, y + h};
  const wchar_t* demos[] = {
      L"双击这一行修改文字",
      L"点方框勾掉，默认加删除线",
      L"右键改皮肤，托盘里新建便签",
  };
  for (const wchar_t* text : demos) {
    Item item;
    item.id = new_id(L"i");
    item.text = text;
    note->items.push_back(std::move(item));
  }
  notes.push_back(std::move(note));
}

std::wstring Store::new_id(const wchar_t* prefix) {
  wchar_t buf[48];
  swprintf_s(buf, L"%s_%llx%02llx", prefix, GetTickCount64(), counter_++ % 256);
  return buf;
}

bool Store::load() {
  wchar_t override_dir[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"ESN_DATA_DIR", override_dir, MAX_PATH) > 0) {
    dir_ = override_dir;
  } else {
    dir_ = local_app_data() + L"\\ESN";
  }
  if (SHCreateDirectoryExW(nullptr, dir_.c_str(), nullptr) != ERROR_SUCCESS &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    logf(L"[store] CreateDirectory failed dir=%s err=%lu", dir_.c_str(), GetLastError());
  }
  logf(L"[store] dir=%s", dir_.c_str());

  std::string raw;
  bool parsed = false;
  if (read_file(path(), &raw) && parse(raw.data(), raw.size())) {
    parsed = true;
  } else if (read_file(backup_path(), &raw) && parse(raw.data(), raw.size())) {
    parsed = true;
  }

  if (!parsed) {
    settings = Settings{};
    notes.clear();
  }
  if (notes.empty()) seed_first_run();

  // 首次运行或损坏后，把规范化后的内容落盘一次
  save();
  return parsed;
}

bool Store::parse(const char* json, size_t size) {
  yyjson_doc* doc = yyjson_read(json, size, YYJSON_READ_NOFLAG);
  if (!doc) return false;

  bool ok = false;
  yyjson_val* root = yyjson_doc_get_root(doc);
  if (yyjson_is_obj(root)) {
    ok = true;
    if (int schema = get_int(root, "schema", 0); schema > kSchema) {
      ok = false;  // 未来版本的数据，宁可不动
    }
    yyjson_val* s = yyjson_obj_get(root, "settings");
    Settings loaded;
    if (yyjson_is_obj(s)) {
      loaded.skin = get_str(s, "skin", L"paper");
      if (loaded.skin != L"light" && loaded.skin != L"dark" &&
          loaded.skin != L"system" && loaded.skin != L"paper") {
        loaded.skin = L"paper";
      }
      loaded.checked_hidden = get_str(s, "checked", L"strike") == L"hide";
      loaded.autostart = get_bool(s, "autostart", false);
    }
    settings = loaded;

    notes.clear();
    yyjson_val* arr = yyjson_obj_get(root, "notes");
    if (yyjson_is_arr(arr)) {
      yyjson_val* nv;
      size_t idx, max;
      yyjson_arr_foreach(arr, idx, max, nv) {
        if (int(notes.size()) >= kMaxNotes) break;
        if (!yyjson_is_obj(nv)) continue;
        auto note = std::make_unique<Note>();
        note->id = get_str(nv, "id");
        note->name = clamp_text(get_str(nv, "name", L"便签"), kMaxNameChars);
        note->pinned = get_bool(nv, "pinned", false);
        yyjson_val* rect = yyjson_obj_get(nv, "rect");
        if (yyjson_is_arr(rect) && yyjson_arr_size(rect) == 4) {
          LONG v[4]{};
          for (size_t i = 0; i < 4; ++i) {
            yyjson_val* e = yyjson_arr_get(rect, i);
            v[i] = yyjson_is_int(e) ? LONG(yyjson_get_int(e)) : 0;
          }
          note->rect = RECT{v[0], v[1], v[0] + v[2], v[1] + v[3]};
        }
        yyjson_val* items = yyjson_obj_get(nv, "items");
        if (yyjson_is_arr(items)) {
          yyjson_val* iv;
          size_t j, jmax;
          yyjson_arr_foreach(items, j, jmax, iv) {
            if (int(note->items.size()) >= kMaxItems) break;
            if (!yyjson_is_obj(iv)) continue;
            Item item;
            item.id = get_str(iv, "id");
            if (item.id.empty()) item.id = new_id(L"i");
            item.text = clamp_text(get_str(iv, "text"), kMaxItemChars);
            item.done = get_bool(iv, "done", false);
            item.indent = get_int(iv, "indent", 0);
            item.remind = get_i64(iv, "remind", 0);
            note->items.push_back(std::move(item));
          }
        }
        if (note->id.empty()) note->id = new_id(L"n");
        notes.push_back(std::move(note));
      }
    }
  }

  yyjson_doc_free(doc);
  return ok;
}

bool Store::save() {
  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  if (!doc) return false;

  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  yyjson_mut_obj_add_int(doc, root, "schema", kSchema);

  yyjson_mut_val* s = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, root, "settings", s);
  yyjson_mut_obj_add_strcpy(doc, s, "skin", narrow(settings.skin).c_str());
  yyjson_mut_obj_add_strcpy(doc, s, "checked", settings.checked_hidden ? "hide" : "strike");
  yyjson_mut_obj_add_bool(doc, s, "autostart", settings.autostart);

  yyjson_mut_val* arr = yyjson_mut_arr(doc);
  yyjson_mut_obj_add_val(doc, root, "notes", arr);
  for (const auto& note : notes) {
    yyjson_mut_val* nv = yyjson_mut_obj(doc);
    yyjson_mut_arr_append(arr, nv);
    yyjson_mut_obj_add_strcpy(doc, nv, "id", narrow(note->id).c_str());
    yyjson_mut_obj_add_strcpy(doc, nv, "name", narrow(note->name).c_str());
    yyjson_mut_obj_add_bool(doc, nv, "pinned", note->pinned);

    yyjson_mut_val* rect = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_int(doc, rect, note->rect.left);
    yyjson_mut_arr_add_int(doc, rect, note->rect.top);
    yyjson_mut_arr_add_int(doc, rect, note->rect.right - note->rect.left);
    yyjson_mut_arr_add_int(doc, rect, note->rect.bottom - note->rect.top);
    yyjson_mut_obj_add_val(doc, nv, "rect", rect);

    yyjson_mut_val* items = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, nv, "items", items);
    for (const auto& item : note->items) {
      yyjson_mut_val* iv = yyjson_mut_obj(doc);
      yyjson_mut_arr_append(items, iv);
      yyjson_mut_obj_add_strcpy(doc, iv, "id", narrow(item.id).c_str());
      yyjson_mut_obj_add_strcpy(doc, iv, "text", narrow(item.text).c_str());
      yyjson_mut_obj_add_bool(doc, iv, "done", item.done);
      yyjson_mut_obj_add_int(doc, iv, "indent", item.indent);
      if (item.remind > 0) yyjson_mut_obj_add_int(doc, iv, "remind", item.remind);
    }
  }

  size_t len = 0;
  char* text = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);
  bool ok = false;
  if (text) {
    ok = write_file_atomic(path(), temp_path(), text, len);
    free(text);
  }
  yyjson_mut_doc_free(doc);
  return ok;
}
