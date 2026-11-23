#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

// ---------- 字符串 ----------
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

// ---------- DPI ----------
inline int spx(int v, int dpi) { return MulDiv(v, dpi, 96); }
inline float fpx(float v, int dpi) { return v * dpi / 96.0f; }

// ---------- 颜色 ----------
D2D1_COLOR_F hex(uint32_t rgb, float alpha = 1.0f);

enum class Skin { Light, Dark, Paper, System };

struct Palette {
  D2D1_COLOR_F bg, raised, text, muted, done, line, accent, danger;
  bool dark = false;
};

Palette palette_for(Skin skin);
bool system_uses_light();
Skin skin_from_key(std::wstring_view key);
const wchar_t* skin_key(Skin skin);
const wchar_t* skin_label(Skin skin);

// ---------- 通用 ----------
struct RectF {
  float left, top, right, bottom;
};

inline D2D1_RECT_F to_d2d(const RECT& r) {
  return D2D1::RectF(float(r.left), float(r.top), float(r.right), float(r.bottom));
}

std::wstring format_uint(unsigned long long value);

// 全局热键
struct Hotkey {
  UINT mods = 0;   // MOD_CONTROL / MOD_ALT / MOD_SHIFT / MOD_WIN
  UINT vk = 0;
  bool valid = false;
};
Hotkey parse_hotkey(const std::wstring& text);
std::wstring format_hotkey(const Hotkey& hk);

// Unix 毫秒时间戳
long long now_ms();
long long tomorrow_nine_ms();
std::wstring format_clock(long long ms);   // HH:mm

// 调试日志：总是输出到调试器；设置 ESN_LOG=<路径> 时追加到文件
void logf(const wchar_t* fmt, ...);

// 进程私有字节（验收口径）
unsigned long long private_bytes();
