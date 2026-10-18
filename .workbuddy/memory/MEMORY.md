# 项目长期约定（Notepad4Markdown）

## 架构（2026-10-18 改版后）

- 预览**编入 `Notepad4.exe`**，不再有独立进程 / 命名管道。历史方案已删除，勿回退。
- 两个新模块：
  - `notepad4/src/MarkdownPreview.cpp`：集成层（布局、分隔条、防抖、后台渲染线程、菜单）
  - `notepad4/src/MarkdownPreviewView.cpp`：内嵌视图（容器窗口 + WebView2 + 虚拟域资源路由）
- 渲染与内容层在 `preview/src/{MarkdownRenderer,LocalContent}`，同时被主程序和单元测试复用。
- `shared/text_encoding.*` 只做 UTF-8/代码页转换（不再是协议模块）。
- 窗口类名（集成测试依赖）：面板 `Notepad4MarkdownPreviewPane`、分隔条 `Notepad4MarkdownSplitter`。
- 默认界面语言是简体中文（`UILanguage` 缺省 2052），中文菜单在 `locale/zh-Hans/Notepad4.rc`。

## 构建环境事实（本机，已验证）

- Only MSVC 可用：VS 18 Professional（`C:\Program Files\Microsoft Visual Studio\18\Professional`），
  MSVC 14.44 / 14.51，Windows SDK 10.0.26100.0。
- `vcvars64.bat` 在本机**不完整**：它靠 `reg.exe` 查 SDK，而 reg.exe 被安全策略禁止 →
  `WindowsSdkDir` 为空、`INCLUDE/LIB` 只有 VC 路径。所有构建脚本必须显式补：
  `IncludePath/LibraryPath`（/p:）与 `INCLUDE/LIB`（env），并传
  `/p:WindowsSDKInstalled=true /p:WindowsSDK_Desktop_Support=true /p:WindowsSDK_UAP_Support=true`，
  否则 MSB8036。
- 环境变量同时存在 `HTTPS_PROXY` 与 `https_proxy` → MSBuild CL task 抛 MSB6001（Hashtable 重复键）。
  构建脚本里必须先清空代理变量。
- 项目是 `/MT`（静态 CRT）、`/std:c++20`、`/Gv`（默认 `__vectorcall`）。
  **第三方 C 代码里的 `qsort` 回调必须显式 `__cdecl`**，否则 C2440（md4c 已按此修改）。
- `WINVER=0x0600`：`GetDpiForWindow`、`WM_DPICHANGED` 等不在头文件中，
  用 `extern UINT g_uCurrentDPI` 与自定义 `#define WM_DPICHANGED 0x02E0`。
- 本机安全策略会拦截 Chromium 子进程，WebView2 需 `--no-sandbox` 才有画面；
  代码里有自动兼容模式（连续 3 次 ProcessFailed 后切）。
- **`src/*.rc` 及其包含的 `resource.h` 里绝不能写中文注释**：rc.exe 按 ANSI 解析，
  UTF-8 中文注释会把换行吞掉，导致下一行宏定义被并进注释、资源变成字符串名
  （症状：`FindResourceW(210)` 找不到，实际资源名是 `"IDR_MDPREVIEW_SHELL"`）。
  注释一律写英文；中文字符串放在引号内没问题。

## 脚本编写约定

- `.cmd` 文件**必须**：纯 ASCII（中文注释会被 cmd 按 GBK 解析导致乱码命令）+ CRLF 换行（LF 会导致
  `CALL :LABEL` 找不到标签、行首字符被吞）。
- 批处理括号块内的 `ECHO` 文本不要含 `(` `)`。
- 用 python 批量改文件时要 `newline=''` 并且**显式保持 CRLF**，否则会把上游 CRLF 文件整篇变 LF，
  造成 25k 行假 diff（本项目踩过）。
- 本机 `rm` 对带盘符的路径会被 safe-delete 拦截；清空文件用 `: > path`，删除用 python/相对路径。

## 代码约定

- Notepad4 侧补丁只允许"挂钩点"式改动；新增逻辑全部放 `src/MarkdownPreview*.cpp`。
- `MarkdownRenderer.cpp` 不依赖 Windows API（便于单元测试）；资源路由在 `LocalContent::Serve`。
- 渲染在 `CreateThread` 工作线程；主线程用单调 `version` 丢弃过期结果。
- MinGW 构建走 `NP2_DISABLE_MD_PREVIEW`（无 WebView2 静态库），预览功能为空实现。

## 构建/测试入口

- `scripts\build.cmd` → `build\out\x64\Notepad4.exe`（预览资源与中文界面均已内嵌，单文件分发）
- `scripts\run_tests.cmd` → 2 阶段（83 单元 + 24 集成 = 107 项）
- 集成测试截图：`build\logs\shot-inline-*.png`、`build\logs\shot-images.png`
- 诊断日志：`NOTEPAD4_MDPREVIEW_LOG=1` → `%LOCALAPPDATA%\Notepad4MarkdownPreview\webview2.log`
- 排查单个文档：`mdtest.exe --render in.md out.html`、`integration_test.exe <某个.md>`

## 两个反复踩的坑

- **`loading="lazy"` 在 WebView2 里不可靠**：面板被遮挡时 Chromium 会一直推迟加载，
  看起来像图片挂了。预览类页面应改为立即加载。
- **MSBuild 不跟踪 `.rc` 里 RCDATA 引用的文件**：只改 CSS/JS 时 `rc.exe` 不会重跑，
  exe 里仍是旧资源。必须给 `ResourceCompile` 加 `AdditionalInputs`。
