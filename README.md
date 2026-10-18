# Notepad4 Markdown 实时预览

[![build](https://github.com/2kil/Notepad4Markdown/actions/workflows/build.yml/badge.svg)](https://github.com/2kil/Notepad4Markdown/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/2kil/Notepad4Markdown?include_prereleases&sort=semver)](https://github.com/2kil/Notepad4Markdown/releases)

为 [Notepad4](https://github.com/zufuliu/notepad4) 增加 Markdown 实时预览：编辑 `.md` 文件时，
按 `Ctrl+Alt+M` 在**同一个窗口内**把编辑器分成左右两栏，右侧显示渲染结果；左侧再怎么改，右侧都会在 250 ms 防抖后跟上，不需要先保存。

> 基线源码：`zufuliu/notepad4` 提交 `bc6c799`（2026-10-18），BSD 3-Clause。
> 本项目是增量补丁，**不改动 Notepad4 原有功能行为**，预览面板可随时关闭。

---

## 0. 结论速览

| 问题 | 结论 |
| --- | --- |
| 1. 真正的 DLL 插件，还是需要主程序补丁？ | **需要主程序补丁**。Notepad4 没有 DLL 插件机制（第 1.2 节）。预览**编译进 `Notepad4.exe`**，不再有独立进程。补丁涉及 11 个文件、`+4271 / −7` 行（其中 4 个是新增的预览模块源文件、1 个是生成的中文资源块）。 |
| 2. 能否预览未保存的编辑内容？ | **能**。内容取自 Scintilla 内存缓冲区（`SCI_GETTEXT`），不读磁盘；集成测试实测「键入 → 页面回报渲染完成」。 |
| 3. 是否支持自动刷新？ | **支持**，默认开启，防抖 250 ms（可配置）；菜单可关闭，关闭后不再安排任何渲染任务。 |
| 4. 图片能显示吗？ | **本地和网络都能显示**：相对路径图片（同目录 / 子目录 / `./` 前缀）、`http(s)://` 远程图片、`data:image/*` 内嵌图片都正常渲染。本地图片仅允许读取**当前文档目录及其子目录**；`javascript:`、`file:` 绝对路径、`../` 越界一律拒绝。详见第 9 节。 |
| 5. 如何构建和安装？ | `scripts\build.cmd` 产出**单个文件** `build\out\x64\Notepad4.exe`：预览用的 CSS/JS/HTML 与简体中文界面资源都已编入 exe，不需要 `res\` 或 `locale\` 目录。详见第 5、6 节。 |
| 6. 哪些已实测、哪些未验证？ | **107 项自动化检查全部通过**（83 项渲染/安全/资源路由 + 24 项真实编辑器集成）。未验证项见第 12 节。 |
| 7. 界面语言？ | **默认简体中文**（`UILanguage` 缺省为 `2052`），首次启动即中文菜单；仍可在「设置 → 语言」切换。 |
| 8. 打开 `.md` 默认看到什么？ | **默认直接进入预览视图**（编辑器收起、整窗阅读）。菜单「Markdown 预览 → 打开 Markdown 文件时显示预览」默认勾选；取消后打开 `.md` 就是原来的编辑视图。 |

---

## 1. 源码调查结论

调查基于未修改的上游源码 `upstream-notepad4/`。

### 1.1 主程序结构与扩展点

| 关注点 | 实际情况（源码位置） |
| --- | --- |
| 入口 | `src/Notepad4.cpp` 的 `wWinMain`；主窗口类名 `Notepad4`（`Notepad4.h: WC_NOTEPAD4`） |
| 消息处理 | `WndProc` 集中分发：`WM_COMMAND → MsgCommand()`、`WM_NOTIFY → MsgNotify()`、`WM_INITMENU → MsgInitMenu()`、`WM_SIZE → MsgSize()`、`WM_TIMER`（原本只有 `ID_AUTOSAVETIMER`） |
| 命令 ID | `src/resource.h`，`IDM_*` 上限是 `IDM_LEXER_LEXER_COUNT = 40739`；预览使用 40960–40964 |
| 菜单 | `src/Notepad4.rc` 的 `IDR_MAINWND`；快捷键表 `IDR_MAINWND ACCELERATORS`；中文在 `locale/zh-Hans/Notepad4.rc` |
| 编辑器实例 | 全局 `HWND hwndEdit`（Scintilla 子窗口，类名 `Scintilla`），调用统一走 `src/SciCall.h` |
| 当前文档 | 全局 `WCHAR szCurFile[MAX_PATH + 40]` |
| 修改状态 | `SCI_GETMODIFY` |
| 文档加载/保存 | `FileLoad()`、`FileSave()` 是「文档身份变化」的汇聚点 |
| 变更通知 | `SCN_MODIFIED`（`MsgNotify`） |
| 多文档模型 | **单窗口单文档**（无标签页），切换文件＝同一窗口内重新 `FileLoad` |
| 布局 | `MsgSize()` 里用 `SetWindowPos(hwndEdit, …, cx, cy)` 让编辑器占满客户区——这是插入分栏的唯一位置 |
| 构建 | `build/VisualStudio/Notepad4.sln`（MSVC，`/std:c++20`、静态 CRT `/MT`、`/Gv` 向量调用约定）；`build/mingw/notepad4.mk` |

### 1.2 是否存在官方插件机制

**不存在。** 证据：

* `src/` 内所有 `LoadLibrary` 只加载系统 DLL（`propsys.dll`、`elscore.dll`、`icu.dll`、`dbghelp.dll`、`ucrtbase.dll`）。
* 全仓库没有 `NPPM_*` 消息定义（Notepad++ 的插件协议在 Notepad4 中完全不存在）。
* `matepath` 在 README 里叫 "file browser plugin"，但实现只是 `ShellExecuteEx` 启动独立 exe 并传路径，是「外部程序 + 命令行」范式。
* 无 `Plugin.h` / 插件注册表 / 扩展点。

### 1.3 现有 Markdown 支持与本需求的边界

Notepad4 内置 Markdown **语法高亮**（`IDM_LEXER_MARKDOWN_GITHUB/GITLAB/PANDOC`），但高亮只做词法着色，
**不具备任何渲染能力**。本项目的渲染完全独立实现，与高亮配置无耦合。

---

## 2. 架构与取舍

### 2.1 最终架构：主窗口内分栏

```
Notepad4.exe（单个进程）
├─ hwndReBar                    工具栏
├─ hwndEdit                     Scintilla 编辑器（左侧）
├─ hwndSplitter                 6 px 分隔条（可拖动 / 双击复位）
├─ hwndPane                     预览面板（右侧）
│    └─ WebView2 子窗口          https://notepad4.mdpreview/ 虚拟域
└─ hwndStatus                   状态栏

渲染链路：
  SCN_MODIFIED → 防抖定时器 → 抓取缓冲区文本 → 工作线程 md4c 渲染
      → WM_MDPREVIEW_RENDERED → 通知页面 fetch /__doc → 排版
```

* **两种显示形态**：分栏预览（编辑器在左、预览在右）与**预览视图**（隐藏编辑器，预览占满客户区）。
  打开 `.md` 时默认进后者，`Ctrl+Alt+M` 或菜单可随时切回编辑。
* **渲染引擎在进程内**：md4c 解析 + 资源路由是普通 C/C++ 代码，无 IPC。
* **排版在 WebView2**：Chromium 的渲染进程仍是独立进程（`msedgewebview2.exe`），
  页面崩溃不会带走编辑器；`ProcessFailed` 回调里会显示中文提示。
* **不需要通信协议**：同进程直接调用，`shared/` 只保留编码转换工具。
* **资源全内嵌**：预览页面的 4 个文件以 RCDATA 编入；简体中文菜单/对话框/字符串作为
  `LANG_CHINESE` 语言块编入同一 exe。启动时按 `UILanguage` 调 `SetThreadUILanguage()`，
  让 `LoadString` / `LoadMenu` / `DialogBox` 命中对应语言块——因此不再需要 `locale\` 与 `res\`。
  只有想用简体中文以外的界面时，才需要外置语言包 DLL。

### 2.2 为什么改成内嵌

任务要求预览显示在**原窗口**内。跨进程 `SetParent` 可以把外部窗口塞进客户区，
但 WebView2 的输入、焦点、DPI 与合成链路都假设宿主窗口属于本进程，跨进程嵌入会出现
焦点丢失、输入法异常、缩放错乱等不可预期行为。因此这里选择把预览模块编入 `Notepad4.exe`。

| 方案 | 结论 |
| --- | --- |
| A. 纯 DLL 插件（零改主程序） | 不可行：没有插件 API |
| B. 独立预览进程 + 跨进程嵌入 | 可行但脆弱（焦点/DPI/输入法），且不满足「原窗口内」的交互质量要求 |
| **C. 预览模块编入主程序 + 分栏** | **采用**：真正的同窗口分栏，拖动分隔条、焦点切换、高 DPI 都由同一 UI 线程管理 |
| D. 独立预览进程 + 独立窗口（旧实现） | 已移除，不满足本次需求 |

代价与缓解：

* `Notepad4.exe` 体积从约 3.0 MB 增至约 3.5 MB（md4c + 预览模块；WebView2 走系统运行时，不打包）。
* WebView2 的 COM 生命周期在编辑器进程内——用 `ComPtr` + 显式 `Close()`/`Reset()` 严格管理，`WM_DESTROY` 时统一释放。
* 部分受限环境会拦截 Chromium 的沙箱子进程，见 2.4 的兼容模式。

### 2.3 渲染引擎选型

| 候选 | 许可证 | 体积 | 结论 |
| --- | --- | --- | --- |
| **md4c 0.5.2** | MIT | 6 个文件、约 350 KB 源码、零外部依赖 | **采用**：C99、快、原生 GFM（表格/任务列表/删除线/自动链接）、可保留原始 HTML 交给白名单清洗 |
| cmark-gfm | BSD-2 | 需要 cmark + extensions + 多个依赖 | 功能更全，但集成成本与体积明显更高 |
| Hoedown / sundown | ISC | 停止维护 | 排除 |
| 自研 | — | — | 正确性风险不可接受 |

排版与代码高亮交给 WebView2 + 本地 `highlight.js` 11.11.1（BSD-3-Clause，随程序分发，**不加载任何 CDN**）。

**对 md4c 的唯一本地修改**：`md4c.c` / `entity.c` 里的两个 `qsort` 比较函数加了 `__cdecl`。
Notepad4 全局用 `/Gv`（`__vectorcall` 缺省），而 CRT 的 `qsort` 回调是 `__cdecl`，不改会编译失败（`C2440`）。
改动已用注释标注，其余代码与上游 `release-0.5.2` 一致。

### 2.4 受限环境下的兼容模式

在启用组策略、EDR 或受限沙箱的机器上，Chromium 的 GPU/utility 子进程可能被反复终止。
此时预览会检测到 `ProcessFailed`（GPU / utility / sandbox-helper 三类），连续 3 次后
自动以 `--disable-gpu --no-sandbox` 重建一次 WebView2，并在面板上提示「正在以兼容模式重新初始化预览引擎…」。

之所以可以接受 `--no-sandbox`：预览页面的内容**全部由本进程生成**（md4c 输出 + 本地 CSS/JS），
不允许远程导航、不执行文档里的脚本、不加载外部资源（第 9 节）。它只在这种系统已经把沙箱破坏掉的环境里生效。

默认参数是 `--disable-gpu --disable-gpu-compositing`：Markdown 是静态文本，软件合成对滚动没有可感知影响，
却能避开大量驱动兼容问题，并降低内存占用。想恢复 GPU 加速，见第 8 节 `browserargs=gpu`。

---

## 3. 目录结构

```
Notepad4Markdown/
├── .github/workflows/build.yml    CI：构建 → 单测 → 打包 → 发 Release
├── upstream.lock                  上游仓库与锁定提交（patch 的基线）
├── patch/0001-…patch              最小集成补丁（相对 upstream.lock 的提交）
├── notepad4/                      ← 生成目录，不入库：由 fetch_upstream.py 拉取并打补丁
│   └── src/
│       ├── MarkdownPreview.h/.cpp        集成层：布局、分隔条、防抖、后台渲染、菜单
│       └── MarkdownPreviewView.h/.cpp    内嵌视图：容器窗口 + WebView2 + 资源路由
├── preview/
│   ├── src/
│   │   ├── MarkdownRenderer.h/.cpp       md4c 封装 + 安全后处理（URL 白名单/HTML 转义）
│   │   └── LocalContent.h/.cpp           渲染结果缓存 + 虚拟域资源路由 + 快照导出
│   └── res/                              构建时以 RCDATA 编入 Notepad4.exe
│       ├── shell.html                    预览页骨架
│       ├── app.css                       排版与明暗主题
│       ├── app.js                        拉取正文 / 主题切换 / 渲染回报
│       └── highlight.min.js              本地代码高亮（无 CDN）
├── shared/
│   └── text_encoding.h/.cpp              UTF-8 / 代码页转换（渲染层与集成层共用）
├── third_party/
│   ├── md4c/                             MIT，Markdown 解析（含 2 处 __cdecl 本地修改）
│   └── webview2/                         Microsoft WebView2 SDK 头文件 + x64 静态导入库
├── tests/
│   ├── mdtest.cpp                        单元测试：渲染、安全、资源路由、快照
│   ├── mdtest.vcxproj                    单测工程（CI 用 msbuild 编译）
│   ├── integration_test.cpp              真实 Notepad4 + 内嵌面板集成测试
│   └── fixtures/                         测试用 Markdown 与图片
├── scripts/
│   ├── fetch_upstream.py                 按 upstream.lock 拉取上游 + git apply 补丁
│   ├── build.cmd                         一键构建（首次运行会先拉取上游）
│   ├── build_notepad4.cmd                只构建编辑器
│   ├── build_locale.cmd                  （可选）生成独立的 zh-Hans 语言包 DLL
│   ├── package.ps1                       打发布包（zip + sha256）
│   ├── gen_locale_rc.py                  从上游 locale 脚本生成可内嵌的中文资源块
│   ├── run_tests.cmd                     构建并运行全部测试
│   └── dev/                              调试用的小工具（截图、窗口枚举）
├── LICENSE                                本仓库新增代码（MIT）
├── LICENSE-THIRD-PARTY.md                 第三方组件许可证
├── build/                                 构建产物（不入库）
│   └── out/x64/Notepad4.exe               单文件，预览资源与中文界面均已内嵌
└── dist/                                  发布包输出（不入库）
```

---

## 4. 第三方依赖

| 依赖 | 版本 | 许可证 | 来源 | 用途 |
| --- | --- | --- | --- | --- |
| md4c | release-0.5.2 | MIT | github.com/mity/md4c | Markdown → HTML |
| WebView2 SDK | 1.0.3351.48 | Microsoft 许可（可再分发） | NuGet `Microsoft.Web.WebView2` | 内嵌浏览器控件 |
| highlight.js | 11.11.1 | BSD-3-Clause | cdnjs（下载后本地分发） | 代码块高亮 |
| WebView2 Runtime | 系统已安装 | Microsoft | 用户系统自带 / 引导安装 | 运行时 |

许可证全文见 `third_party/*/LICENSE*`。**运行时不需要联网**，也不从 CDN 加载任何资源。

---

## 5. 构建

### 5.1 环境要求

* Windows 10/11 x64
* Visual Studio 2019/2022/2026（MSVC v142+），含「使用 C++ 的桌面开发」
* Windows SDK 10
* WebView2 Runtime（Win11 自带；Win10 可能需要安装）

### 5.2 一键构建

```bat
scripts\build.cmd
```

依次完成：

0. 若 `notepad4\` 不存在：`fetch_upstream.py` 按 `upstream.lock` 浅克隆上游提交并应用补丁
   （等价于手动 clone + `git apply`）；
1. `build_notepad4.cmd` → 编译 `Notepad4.exe`（预览模块与 CSS/JS 资源一并编入）；
2. 组装 `build\out\x64\`（只有一个 exe）。

只想拉取/刷新源码树：

```bat
python scripts\fetch_upstream.py
python scripts\fetch_upstream.py --force     :: 删掉 notepad4\ 后重新拉取
```

产物：

```
build\out\x64\
└── Notepad4.exe   编辑器 + 预览 + CSS/JS + 简体中文界面
```

单独构建某一项：

```bat
scripts\build_notepad4.cmd     :: 只要编辑器
scripts\build_locale.cmd       :: （可选）单独生成 zh-Hans 语言包 DLL
```

### 5.3 本机环境上的两个已知坑

这两个问题与代码无关，构建脚本已经内置规避：

1. **`vcvars64.bat` 无法工作**：它通过 `reg.exe` 查询 Windows SDK，而本机安全策略禁止 `reg.exe`，
   导致 `MSB8036: The Windows SDK version was not found`。
   规避：脚本显式传入 `IncludePath` / `LibraryPath` / `ResourceIncludePath`，并加 `WindowsSDKInstalled=true`。
2. **MSBuild 的 CL 任务报 `MSB6001`**：环境里 `HTTPS_PROXY` 与 `https_proxy` 同时存在，CL 任务包装器因此抛异常。
   规避：脚本在调用前清空这两个变量。

### 5.4 把补丁应用到自己的 Notepad4 检出

```bat
git clone https://github.com/zufuliu/notepad4.git
cd notepad4
git apply --check ..\patch\0001-notepad4-markdown-preview.patch
git apply ..\patch\0001-notepad4-markdown-preview.patch
```

补丁只做加法：新增 4 个源文件，修改 6 个已有文件（`Notepad4.cpp` 47 行、资源与工程配置）。
应用后需要把 `preview/`、`shared/`、`third_party/` 三个目录放在仓库**同一父目录**下（工程用相对路径引用），
或者按 5.5 修改工程里的相对路径。

### 5.5 工程改动

`build/VisualStudio/Notepad4.vcxproj` 增加：

* 源文件：`MarkdownPreview.cpp`、`MarkdownPreviewView.cpp`、`preview/src/MarkdownRenderer.cpp`、
  `preview/src/LocalContent.cpp`、`shared/text_encoding.cpp`、`third_party/md4c/*.c`
* 头文件搜索路径：`..\..\..\preview\src`、`..\..\..\shared`、`..\..\..\third_party\md4c`、`..\..\..\third_party\webview2\include`
* 链接：`WebView2LoaderStatic.lib`（`..\..\..\third_party\webview2\x64`）
* 资源的增量依赖：`ResourceCompile` 的 `AdditionalInputs` 列出 4 个 `preview/res/*`
  与生成的中文资源块——否则只改 CSS/JS 时 `rc.exe` 不会重跑，exe 里仍是旧资源。
* 字符集：上游全局只有 `/source-charset:utf-8`，执行字符集仍取系统 ACP。自有源文件
  （`MarkdownPreview.cpp`、`MarkdownPreviewView.cpp`、`MarkdownRenderer.cpp`、
  `LocalContent.cpp`、`text_encoding.cpp`）额外加 `/execution-charset:utf-8`，
  保证窄字符串里的中文在英文版 CI（ACP 1252）上也不会变成 `?`。

MinGW 构建（`build/mingw/notepad4.mk`）会定义 `NP2_DISABLE_MD_PREVIEW` 并排除预览源文件——
它没有 WebView2 的 MSVC 静态库。此时编辑器行为与原生 Notepad4 完全一致，只是菜单里没有预览项。

### 5.6 GitHub 自动构建与发布

仓库不保存上游源码树（`notepad4/` 约 200 MB，含构建产物），只保存
`upstream.lock` + `patch/`。CI 与本地脚本都按同一份锁定信息复现补丁树。

工作流：`.github/workflows/build.yml`（`windows-latest`）

| 步骤 | 命令 | 说明 |
| --- | --- | --- |
| 拉取上游 | `python scripts/fetch_upstream.py` | 按 `upstream.lock` 浅克隆指定提交并 `git apply` 补丁 |
| 编译 | `msbuild notepad4\build\VisualStudio\Notepad4.sln /t:Notepad4` | `Release|x64`，与本地构建同一工程 |
| 单测 | `msbuild tests\mdtest.vcxproj` → `build\out\x64\mdtest.exe` | 83 项渲染/安全/路由检查 |
| 打包 | `scripts\package.ps1 -Version <tag 或 dev-sha>` | 生成 `dist\Notepad4Markdown-<version>-x64.zip` + `.sha256` |
| 产物 | `actions/upload-artifact` | 每次 push/PR 都能下载构建结果 |
| 发布 | `gh release create` | 仅 `v*` 标签触发 |

发布流程：

```bat
git tag v1.0.0
git push origin v1.0.0
```

标签推送后自动创建 Release，附件为 zip 与 sha256，Release Notes 由 GitHub 生成。
同一标签重复推送时改为覆盖附件（`gh release upload --clobber`），不会重复建 Release。

注意事项：

* **升级上游**：改 `upstream.lock` 的 `commit`，重新生成补丁（`git diff`），本地跑通后再提交；
  补丁与锁定提交必须同步，否则 CI 的 `git apply` 会失败。
* **CI 只跑单测**：`integration_test.exe` 需要真实桌面会话与 WebView2 窗口，在无头 runner 上不可靠，
  因此不在 CI 中执行，本地用 `scripts\run_tests.cmd` 跑全量 107 项。
* **不缓存上游源码**：每次流水线重新浅克隆（约 15 s），换取的是一致性；如需加速可自行加 `actions/cache`。

---

## 6. 安装与卸载

**安装**：把 `build\out\x64\` 的内容覆盖到 Notepad4 安装目录即可（便携版直接替换 exe）。

```
Notepad4.exe
```

就这一个文件。里面同时包含：

* 预览页面的 `shell.html` / `app.css` / `app.js` / `highlight.min.js`（RCDATA，ID 210–213）；
* 简体中文的菜单、对话框、字符串表（第二个语言块，`LANG_CHINESE`）。

> 开发时如果 exe 旁边存在 `res\` 目录，会优先读取磁盘文件——改完 CSS/JS 刷新预览即可生效，不必重新编译。

**卸载**：用原始 `Notepad4.exe` 覆盖回去，
再删除 `Notepad4.ini` 里的 `[Markdown Preview]` 段和 `%LOCALAPPDATA%\Notepad4MarkdownPreview\`。

---

## 7. 使用

| 操作 | 快捷键 / 位置 |
| --- | --- |
| 打开 `.md` 时显示预览 | 「查看 → Markdown 预览 → 打开 Markdown 文件时显示预览」（默认勾选） |
| 打开 / 关闭预览面板 | `Ctrl+Alt+M`，或「查看 → Markdown 预览 → 显示预览面板」 |
| 立即刷新 | `Ctrl+Alt+V`，或菜单「刷新预览」 |
| 自动刷新开关 | 菜单「自动刷新」（勾选状态即当前状态） |
| 在浏览器中打开 | 菜单「在浏览器中打开」——导出独立 HTML 后用默认浏览器打开 |
| 关闭面板 | 菜单「关闭预览面板」 |
| 调整宽度 | 拖动中间分隔条；双击分隔条恢复「客户区一半」 |
| 切换主题 | 预览页右下角的 ◐ 按钮（跟随系统 / 浅色 / 深色） |

两种形态：

* **预览视图**（默认）：打开 `.md` 自动进入。编辑器隐藏，渲染结果占满窗口，适合阅读与核对排版。
  按 `Ctrl+Alt+M`、菜单「显示预览面板」或「关闭预览面板」即可回到编辑视图。
* **分栏预览**：手动按 `Ctrl+Alt+M` 打开时进入，编辑器在左、预览在右，可边改边看。

行为说明：

* 预览视图只影响显示，**不会**修改文档；切回编辑后未保存的修改、光标位置、滚动位置都还在。
* 打开非 Markdown 文件（`.txt`、`.cpp` 等）会自动退出预览视图，回到普通编辑。
* 面板打开时编辑器自动让出右侧空间，状态栏、工具栏、滚动位置全部保留。
* 面板关闭后编辑器立刻恢复占满客户区，**不会**关闭或修改文档。
* 正文铺满面板，左右各留固定 **30 px**，不再限制行宽或居中；表格、代码块超宽时内部横向滚动。
* 首次打开面板会启动 WebView2（约 0.5–1 s）；之后再开关几乎瞬时。
* 预览跟随编辑器的明暗配色（读取 Scintilla 默认样式背景亮度）。

---

## 8. 配置

`Notepad4.ini` 的 `[Markdown Preview]` 段（全部可选）：

| 键 | 默认值 | 说明 |
| --- | --- | --- |
| `openmdpreview` | `1` | 打开 `.md`/`.markdown` 等文件时是否进入预览视图。`0` 则保持原编辑视图 |
| `autorefresh` | `1` | 是否自动刷新。`0` 时只有手动刷新会重新渲染 |
| `debounce` | `250` | 防抖毫秒数，允许 30–5000 |
| `maxsizemb` | `32` | 单次渲染的文档体积上限（MB），超过则提示并跳过 |
| `panelwidth` | `0` | 面板宽度（DIP）。`0` 表示客户区一半；拖动分隔条会自动写入 |
| `browserargs` | 空 | WebView2 附加参数。`gpu` = 恢复 GPU 加速；也可填任意 Chromium 参数 |

示例：

```ini
[Markdown Preview]
openmdpreview=1
autorefresh=1
debounce=200
panelwidth=560
```

---

## 9. 安全模型

预览页运行在虚拟域 `https://notepad4.mdpreview/`，**不存在本地 HTTP 服务**，
所有请求都在进程内由 `LocalContent::Serve()` 处理。

### 9.1 页面侧

* **原始 HTML 白名单重建**：GitHub 风格 README 大量使用 `<div align>`、`<img>`、`<details>` 排版，
  完全禁用会把文档变成一堆源码。这里保留原始 HTML，但在 C++ 层按标签/属性白名单**重新生成**：
  * 标签白名单（约 70 个）：`a/abbr/b/blockquote/br/code/details/div/dl/em/h1-h6/hr/img/li/ol/p/pre/
    table/td/tr/ul/...`；
  * 危险容器（`script/style/iframe/object/embed/svg/math/form/textarea/...`）**连同内容整段丢弃**；
  * 属性按标签白名单放行（`href/src/alt/width/height/align/colspan/open/...`），
    所有 `on*` 事件属性、`srcdoc`、`formaction` 一律不保留；
  * URL 属性只允许 `http/https/mailto`、相对路径与锚点；`src` 额外允许 `data:image/*`；
  * `style` 属性保留，但含 `expression`、`javascript:`、`behavior`、`@import` 时丢弃；
  * `<input>` 只允许 `type="checkbox"`（任务列表），其它类型整个标签丢弃。
* CSP 通过 `shell.html` 的 `<meta http-equiv="Content-Security-Policy">` 注入：
  `default-src 'none'` 起步；实时预览放行同源样式/脚本与 `img-src https: http: data:`，
  导出的离线快照额外放行 `file:`（用于重写后的本地图片路径）。
* 页面里没有 `eval`、没有内联事件处理器；外部链接由宿主 `NavigationStarting` 拦截后交给系统浏览器。

### 9.2 URL 白名单

`MarkdownRenderer` 对每个链接和图片执行 `IsUrlAllowed()`：

* 允许：`http:`、`https:`、`mailto:`、相对路径；图片额外允许 `data:image/*`（单张 ≤ 2 MB）。
* 拒绝：`javascript:`、`vbscript:`、`file:`、`data:`（非图片）、
  含控制字符的伪协议（例如 `java\tscript:`）、协议相对 `//host`。
* 被拒绝的 URL 会被替换为占位符，不会进入 DOM。

### 9.3 本地资源读取范围

`LocalContent` 只回答白名单路径：

| 路径 | 内容 |
| --- | --- |
| `/__doc` | 当前渲染结果 |
| `/__meta` | 标题、编码、修改状态、错误文本、版本号 |
| `/__app/shell.html`、`/__app/app.css`、`/__app/app.js`、`/__app/highlight.min.js` | 本地资源 |
| `/<相对路径>`（例如 `/img/pic.png`） | 文档目录内的**图片**（png/jpg/gif/webp/svg/bmp/ico/avif） |

规则：

* 路径规范化后必须仍位于文档目录之内；`..`、百分号编码的 `%2e%2e`、绝对路径、盘符一律 404。
* 非图片扩展名（`.txt`、`.exe`、`.md`…）一律 404——预览永远读不到任意本地文件。
* 未命名（未保存）文档没有目录，所有相对资源请求都会 404。

### 9.4 图片加载

| 来源 | 处理方式 | 说明 |
| --- | --- | --- |
| 相对路径 `img/a.png`、`./a.png` | 请求 `https://notepad4.mdpreview/img/a.png`，由 `LocalContent` 在文档目录内解析 | 页面从根路径 `/` 加载，相对路径才能正确落到文档目录 |
| 子目录 `assets/x/y.png` | 同上 | 允许；`../` 与编码绕过一律 403 |
| 网络图片 `https://…`、`http://…` | 由 WebView2 直接下载 | 需要网络；离线时显示 alt 文本并加虚线框 |
| `data:image/*` | 直接内联 | 单张上限 2 MB，超出即拒绝 |
| `javascript:`、`file:`、协议相对 `//host` | 替换为占位图并计入 `blockedUrls` | 不进入 DOM |

生成 `<img>` 时**不加** `loading="lazy"`：面板可能被其它窗口遮挡，Chromium 会
无限期推迟懒加载，看起来像"图片加载不出来"。预览是本地文档，直接加载的成本可以忽略。

页面在上报渲染结果时会同时给出 `images: {total, loaded, failed}`，
宿主只用于健康检查（不读取图片内容），集成测试据此断言"图片真的显示了"。

### 9.5 导航与脚本策略

* `AreDefaultContextMenusEnabled = FALSE`、`AreDevToolsEnabled = FALSE`、`AreBrowserAcceleratorKeysEnabled = FALSE`。
* `NavigationStarting` 只允许虚拟域；其它 URL 取消并 `ShellExecute`。
* `NewWindowRequested` 一律 `Handled = TRUE`，仅 `http/https` 交给系统浏览器。
* 宿主只接收页面主动上报的 `{type, version, chars}`，**从不**把页面消息当作命令执行。

### 9.6 日志脱敏

诊断日志（`NOTEPAD4_MDPREVIEW_LOG=1` 时启用）只记录：环境初始化结果、导航是否成功、
进程失败类型、渲染版本号与页面字符数。**不记录文档内容、不记录文件路径**。

---

## 10. 实时同步设计

```
SCN_MODIFIED ──▶ SetTimer(debounce) ──▶ 到期
                                          │
                       主线程：抓取缓冲区文本（SCI_GETTEXT）+ 元信息
                                          │ 投递任务（新任务覆盖旧任务）
                                          ▼
                       工作线程：md4c 渲染 → 写入 LocalContent（加锁）
                                          │ PostMessage(WM_MDPREVIEW_RENDERED, version)
                                          ▼
                       主线程：version 比较（丢弃过期）→ ExecuteScript reload
                                          │
                       页面：fetch /__doc → 替换 DOM → postMessage 回报
```

关键点：

1. **未保存内容**：读的是 Scintilla 内存缓冲区，不碰磁盘。
2. **防抖**：`SetTimer` 复用同一 ID，等价于「最后一次输入后 250 ms 触发」。
3. **不阻塞 UI**：md4c 在工作线程跑；主线程只做取文本（Scintilla 只能在主线程访问）与页面通知。
4. **任务合并**：工作线程一次只保留最新任务，连续输入不会排队。
5. **防竞态**：每次请求带单调递增 `version`，主线程与页面都丢弃旧版本，切换文档不会出现旧内容覆盖新内容。
6. **文件切换 / 保存 / 另存为 / 重新载入**：`FileLoad` / `FileSave` 成功后立即全量刷新。
7. **自动刷新关闭**：`SCN_MODIFIED` 直接返回，不设置任何定时器。
8. **错误可见**：渲染失败、体积超限、WebView2 异常都会在面板或对话框里给出中文提示。

---

## 11. 测试与验收

### 11.1 一键执行

```bat
scripts\build.cmd
scripts\run_tests.cmd
```

### 11.2 阶段 1：单元 / 组件测试（`mdtest.exe`，83 项）

覆盖：Markdown 渲染（标题/段落/引用/分隔线/有序无序列表/任务列表/粗斜体/删除线/行内代码/
代码块/表格/链接/图片/HTML 实体/中文与 emoji）、GFM 扩展、HTML 白名单（`<div align>`/`<details>`
保留、`<script>` 连内容丢弃、`onerror` 移除、`javascript:` 协议移除）、资源路由（越界、编码绕过、
非图片）、版本号单调性、快照导出。

### 11.3 阶段 2：真实编辑器集成（`integration_test.exe`，24 项）

启动真实的 `Notepad4.exe`，用 `WM_COMMAND` 触发命令，断言：

```
PASS  编辑器主窗口出现
PASS  找到 Scintilla 编辑控件
PASS  主菜单中包含 Markdown 预览子菜单
PASS  打开 .md 自动进入预览视图
PASS  预览视图隐藏编辑器
PASS  预览视图下预览占满客户区
PASS  页面完成渲染并回报宿主
PASS  关闭预览后编辑器重新出现
PASS  编辑器恢复占满客户区
PASS  取消勾选后打开 .md 为编辑视图
PASS  手动打开后预览面板与分隔条同时出现
PASS  预览面板有实际宽度
PASS  编辑器被压缩到左侧（原窗口内分栏）
PASS  三部分宽度之和等于客户区宽度
PASS  面板内出现 WebView2 渲染子窗口
PASS  输入未保存内容后布局保持不变
PASS  拖动分隔条改变预览面板宽度
PASS  双击分隔条恢复默认宽度
PASS  手动刷新后预览面板仍然可见
PASS  关闭后预览面板与分隔条隐藏
PASS  关闭后编辑器恢复占满客户区
PASS  预览可重复打开
PASS  全程编辑器进程未退出
```

其中「页面完成渲染并回报宿主」是端到端证据：页面在 DOM 更新后通过
`window.chrome.webview.postMessage` 上报渲染字符数，测试读取诊断日志确认数值 > 50。

测试同时把窗口截图写入 `build/logs/shot-inline-*.png`，便于人工复核。

### 11.4 实测结果（本机 Windows 11 + VS 2026 + WebView2 155）

```
Stage 1  unit tests            83 项，失败 0 项        exit 0
Stage 2  editor integration    24 项，失败 0 项        exit 0
合计                            107 项，失败 0 项
```

`git apply --check` 验证补丁可干净应用到上游提交 `bc6c799`。

### 11.5 场景对照

| 场景 | 状态 | 说明 |
| --- | --- | --- |
| 打开 `.md` 直接进入预览视图 | ✅ | 默认开启，集成测试覆盖 |
| 取消开关后打开 `.md` 为编辑视图 | ✅ | 集成测试（切换菜单 + 重新加载） |
| 普通 Markdown 文件 | ✅ | `tests/fixtures/basic.md` |
| GitHub 风格 README（HTML 排版） | ✅ | 用 `TikTokDownloader/README.md` 实测：居中容器、徽章、`<details>`、emoji 均正常 |
| HTML 中的本地图片 | ✅ | 页面从根路径加载，`./static/a.png` 正确解析到文档目录 |
| 中文 / Unicode / emoji | ✅ | 单元测试覆盖，截图可见中文排版 |
| 未保存内容实时预览 | ✅ | 集成测试键入后页面重新渲染 |
| 表格 / 任务列表 / 引用 / 代码块 | ✅ | 截图与单元测试 |
| 相对路径图片 | ✅ | 进程内白名单路由，越界 404 |
| 网络图片（`http(s)://`） | ✅ | 由 WebView2 直接下载；离线时显示 alt 文本与虚线框 |
| `data:image/*` 内嵌图片 | ✅ | 单张上限 2 MB |
| 图片实际加载成功 | ✅ | 页面把 `{total,loaded,failed}` 回报宿主，集成测试断言 `loaded ≥ 1` |
| 超长文档 | ⚠️ 代码就绪，未做 100 MB 压测 | 32 MB 上限可配置 |
| 连续快速输入 | ✅ | 防抖 + 任务合并 + 版本丢弃 |
| 多文档切换 | ⚠️ 代码路径已接（`FileLoad`/`FileSave`），未自动化 | 单文档模型下等价于重新加载 |
| 关闭后重新打开 | ✅ | 集成测试 |
| 原始 HTML / 恶意链接 | ✅ | 单元测试 |
| 预览渲染进程异常退出 | ✅ | `ProcessFailed` → 中文提示 / 兼容模式 |
| 原有编辑、保存、查找、快捷键 | ✅ | 集成测试确认进程存活、布局恢复 |

---

## 12. 已知限制与未验证项

1. **没有 DLL 插件形态**：预览编入 `Notepad4.exe`，升级上游时需要重新应用补丁。
2. **MinGW 构建不含预览**：没有 WebView2 的 MSVC 静态库；已用 `NP2_DISABLE_MD_PREVIEW` 让该构建保持可用。
3. **仅 x64 实机验证**：工程为 x64 配置；ARM64/Win32 未构建验证。
4. **多显示器 / 高 DPI**：代码在 `WM_DPICHANGED` 里重排并更新 `RasterizationScale`，但未在多屏混合缩放环境下实测观感。
5. **超大文档**：未做 100 MB 级压测；当前上限 32 MB（可配置），超过则提示并跳过。
6. **延迟渲染路径未压测**：工作线程渲染在 1 MB 级文档上约 10–20 ms，未做系统性基准。
7. **内嵌语言只有简体中文**：想用日/韩/繁体等界面，仍需把对应的 `locale\<lang>\Notepad4.dll` 放到 exe 旁（上游机制）。
8. **受限环境的兼容模式**：本机（安全策略禁用 `reg.exe`、拦截 Chromium 子进程）需要 `--no-sandbox` 才能出画面；
   在正常 Windows 上不会触发。触发时会自动降级并提示；初始化失败（如 `0x8007139F`）会自动退避重试 4 次。
9. **网络图片依赖外网**：远程图片由 WebView2 下载，首次显示有网络延迟（通常 0.2–2 s）；离线时保留 alt 文本。
   本地图片与 `data:` 图片不受影响。

---

## 13. 补丁说明

`patch/0001-notepad4-markdown-preview.patch`（11 个文件，`+4271 / −7`）

| 文件 | 改动 |
| --- | --- |
| `src/MarkdownPreview.h` / `.cpp` | **新增**：面板生命周期、布局、分隔条、防抖、后台渲染线程、菜单状态 |
| `src/MarkdownPreviewView.h` / `.cpp` | **新增**：容器窗口 + WebView2 + 虚拟域资源路由 + 兼容模式 |
| `src/Notepad4.cpp` | 12 处挂钩：`WM_SIZE` 布局、`WM_MDPREVIEW_RENDERED`、`WM_TIMER`、`WM_DPICHANGED`、`WM_SETFOCUS`、命令分发、菜单初始化、`SCN_MODIFIED`、`FileLoad`、`FileSave`、退出清理；另将默认界面语言改为简体中文 |
| `src/Notepad4.rc` | 英文菜单项、快捷键、字符串；4 个预览资源以 RCDATA 编入 exe；末尾 include 中文资源块 |
| `src/resource.h` | 6 个命令 ID（40960–40965）、4 个字符串 ID、4 个资源 ID（210–213） |
| `locale/zh-Hans/Notepad4.rc` | 中文菜单项、快捷键、字符串 |
| `build/VisualStudio/Notepad4.vcxproj` | 新增源文件、头文件路径、WebView2 静态库；`ResourceCompile` 的 `AdditionalInputs` 声明内嵌资源依赖 |
| `build/generated/Notepad4.zh-Hans.rc` | **新增**：从上游 locale 脚本生成的中文资源块（2067 行，去掉图标/位图/清单等重复资源） |
| `build/mingw/notepad4.mk` | 定义 `NP2_DISABLE_MD_PREVIEW` 并排除预览源文件 |

上游升级后重新应用：

```bat
git apply --3way ..\patch\0001-notepad4-markdown-preview.patch
```

---

## 14. 诊断

设置环境变量 `NOTEPAD4_MDPREVIEW_LOG=1` 后启动编辑器，会在
`%LOCALAPPDATA%\Notepad4MarkdownPreview\webview2.log` 写入关键事件：

```
[14:56:39.591] environment ready
[14:56:40.475] NavigationCompleted success=1 status=0
[14:56:41.102] page message: {"type":"rendered","version":1,"chars":842}
```

日志只含状态与计数，不含文档内容（第 9.5 节）。

常见问题：

| 现象 | 原因与处理 |
| --- | --- |
| 面板显示「未找到 Microsoft Edge WebView2 运行时」 | 未安装 WebView2 Runtime，安装后重试 |
| 面板显示「缺少 Markdown 预览资源目录」 | exe 不是本项目的构建产物（不含内嵌资源） |
| 面板提示渲染进程退出 | 系统拦截了 Chromium 子进程；程序会自动尝试兼容模式，仍失败则设置 `browserargs=--no-sandbox --disable-gpu` |
| 预览图片不显示 | 文档未保存（无目录），或图片在文档目录之外 |
| 界面是英文 | ini 里 `UILanguage` 被改成非中文（改回 `2052` 或删除该键后重启） |
