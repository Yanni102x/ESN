# ESN

> **Electronic Sticky Note** —— 贴在桌面上的待办便签。多张并存、勾完即走、常驻内存不超过 10 MiB。

多便签 · 勾选待办 · 到点提醒 · 托盘常驻 · 四套皮肤 · 单 exe，私有内存 < 10 MiB

## 下载与安装

发布页提供两种包，都**没有代码签名**——Windows SmartScreen 可能提示"未知发布者"，点「更多信息 → 仍要运行」即可。

- **便携版** `ESN-1.0.0-x64-portable.zip`：解压到任意目录，双击 `ESN.exe`。包里带 `ESN.portable` 标记，**有它时数据写在 exe 同目录的 `data\`**，整个文件夹放 U 盘就能带走。
- **安装版** `ESN-1.0.0-x64-setup.exe`：按用户装到 `%LOCALAPPDATA%\Programs\ESN`，不需要管理员权限；卸载时会清掉开机启动项，用户数据默认保留。

数据位置：

| 模式 | 数据目录 |
| --- | --- |
| 便携（exe 同目录有 `ESN.portable`） | `<exe 目录>\data\data.json` |
| 普通 | `%LOCALAPPDATA%\ESN\data.json` |

校验：下载目录里的 `SHA256SUMS.txt`。

## 提醒怎么用

点条目右侧的 `◷` 时钟图标（新建条目时它就在编辑框右边），选 10 / 30 / 60 分钟后、明天 9:00，或「自定义…」挑具体日期和时间；右键「提醒我」「Ctrl+Shift+R」是同一入口。设置后行尾显示 `HH:mm`（过期变红）。
到点时：便签自动从托盘弹出 → 置顶、任务栏闪烁 → 右下角弹系统通知；15 秒后自动取消临时置顶（除非这张便签本来就是置顶状态）。勾掉条目会自动取消它的提醒。

## 收起 / 恢复

- 标题栏右上角 `—`，或右键「隐藏到托盘」，或 `Ctrl+H`
- 标题栏的图钉是置顶开关，点亮后便签始终压在最上层
- 恢复：**单击托盘图标**（或双击、托盘菜单「显示全部」、`Ctrl+Alt+H`）
- 隐藏期间提醒照常触发，到点会自己弹出来

内存（Release，私有字节）：单张便签 **6.9 MiB**，12 便签 × 200 条 **8.5–9.0 MiB**，上限 10 MiB。

## 构建

需要 Visual Studio 2022 Build Tools（MSVC v143）、Windows SDK 10、CMake 3.25+、Ninja。

```powershell
.\scripts\build.ps1            # 配置 + Release 构建，产物在 build\dist\ESN.exe
.\scripts\build.ps1 -Clean     # 从零重建
```

手工构建（已装好 VS 开发者环境）：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

说明：CMake 生成的 VS 工程也能编，但仓库默认用 Ninja；原因是 MSBuild 对「资源里已带 RT_MANIFEST + 链接器再生成清单」的组合会报 LNK1123，工程里已经用 `/MANIFEST:NO` 规避，Ninja 路径验证最充分。

## 运行与数据

- 直接双击 `build\dist\ESN.exe`，无安装、无运行时依赖（静态 CRT）。
- 数据：`%LOCALAPPDATA%\ESN\data.json`（`.bak` 备份，`.tmp` 中间态）。
- 调试开关（可选）：
  - `ESN_DATA_DIR=<目录>`：换数据目录（测试用）
  - `ESN_LOG=<文件>`：把启动与关键路径日志写文件

## 内存

```powershell
.\scripts\measure-memory.ps1 -Notes 12 -Items 200
```

脚本会生成对应规模的 `data.json`，启动程序，静置 5 s 后采样 15 s，输出私有字节峰值与判定。当前基线：

| 场景 | 私有字节 |
| --- | --- |
| 1 便签 × 0 条 | 6.39 MiB |
| 6 便签 × 50 条 | 7.57 MiB |
| 12 便签 × 50 条 | 7.98 MiB |
| 12 便签 × 200 条 | 8.11 MiB |

## 关键实现决定

**渲染目标必须是 `ID2D1DCRenderTarget`，且全局共享一个。** `ID2D1HwndRenderTarget` 会创建 D3D11 设备与交换链，实测让进程从 2.8 MiB 直接涨到 36.9 MiB，10 MiB 预算当场失守。换成软件光栅的 DC 渲染目标后，全部窗口共用 4.6 MiB 级别的底座，D2D/DWrite 的排版、删除线、ClearType 效果都不受影响。

文字编辑用原生 `EDIT` 浮层做兜底：一行一个编辑位置，天然拿到输入法、选区、剪贴板，避免自绘编辑器在 IME 上的长尾问题；M2 再评估是否换成完全自绘。

## 目录

```
src/
  main.cpp           启动、单实例、DPI/COM 初始化
  app.{h,cpp}        托盘、热键、菜单、便签窗口管理、共享渲染目标
  note_window.{h,cpp}便签窗口：绘制、命中测试、滚动、编辑、持久化触发
  store.{h,cpp}      JSON 读写、原子替换、容量上限
  common.{h,cpp}     字符串/DPI/调色板/日志
res/                 图标（esn.ico / SVG 源）、manifest、版本信息
scripts/             构建、打包（package.ps1）、内存测量
installer/           Inno Setup 安装脚本
docs/ESN-方案.md   完整方案（含 M0 实测记录）
third_party/yyjson/  唯一的第三方依赖（MIT）
```

## 隐私与安全

- **不联网**：代码里没有任何 socket / WinHTTP / WinINet 调用，也没有更新检查
- **不收集**：无遥测、无崩溃上报、无日志外传
- **数据只在本地**：`%LOCALAPPDATA%\ESN\data.json`（含 `.bak` 备份）
- **唯一的系统写入**：勾选「开机启动」时写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 的 `ESN` 值，取消即删除

## 许可

MIT，见 [LICENSE](LICENSE)。第三方依赖 yyjson 同 MIT，见 [third_party/yyjson/LICENSE](third_party/yyjson/LICENSE)。
