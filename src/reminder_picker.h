#pragma once

#include "common.h"

// 打开“提醒时间”选择窗（模态）。返回 true 表示用户确认；
// *cleared 为 true 时表示选择“清除提醒”，否则 *out_ms 为本地时间对应的 Unix 毫秒。
bool pick_reminder_time(HWND owner, const Palette& palette, long long initial_ms,
                        long long* out_ms, bool* cleared);
