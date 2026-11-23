#include "common.h"

#include <psapi.h>

#include <cstdarg>
#include <cstdio>

std::wstring widen(std::string_view utf8) {
  if (utf8.empty()) return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
  if (len <= 0) return {};
  std::wstring out(size_t(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), out.data(), len);
  return out;
}

std::string narrow(std::wstring_view wide) {
  if (wide.empty()) return {};
  int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
  if (len <= 0) return {};
  std::string out(size_t(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), out.data(), len, nullptr, nullptr);
  return out;
}

D2D1_COLOR_F hex(uint32_t rgb, float alpha) {
  return D2D1::ColorF(float((rgb >> 16) & 0xFF) / 255.0f,
                      float((rgb >> 8) & 0xFF) / 255.0f,
                      float(rgb & 0xFF) / 255.0f, alpha);
}

Palette palette_for(Skin skin) {
  Palette p;
  switch (skin) {
    case Skin::Dark:
      p.bg = hex(0x1E2023);
      p.raised = hex(0x26292D);
      p.text = hex(0xE7EAEE);
      p.muted = hex(0x8C939D);
      p.done = hex(0x6F777F);
      p.line = hex(0x33383E);
      p.accent = hex(0x8DA3A2);
      p.danger = hex(0xD9776E);
      p.dark = true;
      break;
    case Skin::Paper:
      p.bg = hex(0xF6EFDF);
      p.raised = hex(0xEFE5D0);
      p.text = hex(0x2E3238);
      p.muted = hex(0x7B6E57);
      p.done = hex(0x857A64);
      p.line = hex(0xDCCFB2);
      p.accent = hex(0x7C8C8C);
      p.danger = hex(0xA5524A);
      break;
    case Skin::Light:
    case Skin::System:
    default:
      p.bg = hex(0xFBFBFD);
      p.raised = hex(0xF1F3F6);
      p.text = hex(0x1F2328);
      p.muted = hex(0x7A828C);
      p.done = hex(0x8A929B);
      p.line = hex(0xE1E5EA);
      p.accent = hex(0x6E7F7F);
      p.danger = hex(0xB4453C);
      break;
  }
  return p;
}

bool system_uses_light() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                     reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
  }
  return value != 0;
}

Skin skin_from_key(std::wstring_view key) {
  if (key == L"light") return Skin::Light;
  if (key == L"dark") return Skin::Dark;
  if (key == L"system") return Skin::System;
  return Skin::Paper;
}

const wchar_t* skin_key(Skin skin) {
  switch (skin) {
    case Skin::Light: return L"light";
    case Skin::Dark: return L"dark";
    case Skin::System: return L"system";
    default: return L"paper";
  }
}

const wchar_t* skin_label(Skin skin) {
  switch (skin) {
    case Skin::Light: return L"浅色";
    case Skin::Dark: return L"深色";
    case Skin::System: return L"跟随系统";
    default: return L"宣纸";
  }
}

std::wstring format_uint(unsigned long long value) {
  wchar_t buf[32];
  swprintf_s(buf, L"%llu", value);
  return buf;
}

void logf(const wchar_t* fmt, ...) {
  wchar_t buffer[1200];
  va_list args;
  va_start(args, fmt);
  vswprintf_s(buffer, fmt, args);
  va_end(args);
  OutputDebugStringW(buffer);
  OutputDebugStringW(L"\n");

  wchar_t path[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"ESN_LOG", path, MAX_PATH) == 0) return;
  FILE* file = nullptr;
  if (_wfopen_s(&file, path, L"a, ccs=UTF-8") == 0 && file) {
    fwprintf(file, L"%s\n", buffer);
    fclose(file);
  }
}

unsigned long long private_bytes() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters))) {
    return static_cast<unsigned long long>(counters.PrivateUsage);
  }
  return 0;
}

long long now_ms() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER v{};
  v.LowPart = ft.dwLowDateTime;
  v.HighPart = ft.dwHighDateTime;
  return static_cast<long long>((v.QuadPart - 116444736000000000ULL) / 10000ULL);
}

long long tomorrow_nine_ms() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  st.wDay += 1;
  st.wHour = 9;
  st.wMinute = 0;
  st.wSecond = 0;
  st.wMilliseconds = 0;
  FILETIME local{};
  if (!SystemTimeToFileTime(&st, &local)) return now_ms() + 12LL * 3600 * 1000;
  FILETIME utc{};
  if (!LocalFileTimeToFileTime(&local, &utc)) return now_ms() + 12LL * 3600 * 1000;
  ULARGE_INTEGER v{};
  v.LowPart = utc.dwLowDateTime;
  v.HighPart = utc.dwHighDateTime;
  return static_cast<long long>((v.QuadPart - 116444736000000000ULL) / 10000ULL);
}

std::wstring format_clock(long long ms) {
  if (ms <= 0) return {};
  FILETIME utc{};
  ULARGE_INTEGER v{};
  v.QuadPart = static_cast<unsigned long long>(ms) * 10000ULL + 116444736000000000ULL;
  utc.dwLowDateTime = v.LowPart;
  utc.dwHighDateTime = v.HighPart;
  FILETIME local{};
  FileTimeToLocalFileTime(&utc, &local);
  SYSTEMTIME st{};
  FileTimeToSystemTime(&local, &st);
  wchar_t buf[16];
  swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
  return buf;
}

namespace {
const struct {
  const wchar_t* name;
  UINT vk;
} kKeyNames[] = {
    {L"Space", VK_SPACE},     {L"Enter", VK_RETURN},   {L"Tab", VK_TAB},
    {L"Esc", VK_ESCAPE},      {L"Left", VK_LEFT},      {L"Right", VK_RIGHT},
    {L"Up", VK_UP},           {L"Down", VK_DOWN},      {L"Home", VK_HOME},
    {L"End", VK_END},         {L"PageUp", VK_PRIOR},   {L"PageDown", VK_NEXT},
    {L"Insert", VK_INSERT},   {L"Delete", VK_DELETE},
};
}

Hotkey parse_hotkey(const std::wstring& text) {
  Hotkey hk;
  std::vector<std::wstring> parts;
  std::wstring token;
  for (wchar_t c : text) {
    if (c == L'+' ) {
      parts.push_back(token);
      token.clear();
    } else if (c != L' ') {
      token.push_back(c);
    }
  }
  parts.push_back(token);
  if (parts.empty()) return hk;

  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    std::wstring t = parts[i];
    for (auto& ch : t) ch = wchar_t(towupper(ch));
    if (t == L"CTRL" || t == L"CONTROL") hk.mods |= MOD_CONTROL;
    else if (t == L"ALT") hk.mods |= MOD_ALT;
    else if (t == L"SHIFT") hk.mods |= MOD_SHIFT;
    else if (t == L"WIN") hk.mods |= MOD_WIN;
    else return hk;
  }

  std::wstring key = parts.back();
  if (key.size() == 1) {
    wchar_t c = wchar_t(towupper(key[0]));
    if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) hk.vk = c;
  } else if (key.size() >= 2 && (key[0] == L'F' || key[0] == L'f')) {
    const int n = _wtoi(key.c_str() + 1);
    if (n >= 1 && n <= 24) hk.vk = VK_F1 + n - 1;
  }
  if (!hk.vk) {
    for (const auto& entry : kKeyNames) {
      if (_wcsicmp(key.c_str(), entry.name) == 0) {
        hk.vk = entry.vk;
        break;
      }
    }
  }
  hk.valid = hk.vk != 0 && hk.mods != 0;
  return hk;
}

std::wstring format_hotkey(const Hotkey& hk) {
  if (!hk.valid) return {};
  std::wstring out;
  auto add = [&](const wchar_t* name) {
    if (!out.empty()) out += L'+';
    out += name;
  };
  if (hk.mods & MOD_CONTROL) add(L"Ctrl");
  if (hk.mods & MOD_ALT) add(L"Alt");
  if (hk.mods & MOD_SHIFT) add(L"Shift");
  if (hk.mods & MOD_WIN) add(L"Win");

  for (const auto& entry : kKeyNames) {
    if (hk.vk == entry.vk) {
      add(entry.name);
      return out;
    }
  }
  if (hk.vk >= VK_F1 && hk.vk <= VK_F24) {
    add((L"F" + std::to_wstring(hk.vk - VK_F1 + 1)).c_str());
  } else if (hk.vk < 128) {
    wchar_t buf[2]{wchar_t(hk.vk), 0};
    add(buf);
  }
  return out;
}
