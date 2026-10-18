# 第三方组件与许可证

本项目的发布包包含一个可执行文件 `Notepad4.exe`。该文件由上游 Notepad4 源码
加上本仓库的 Markdown 预览补丁编译而成，内含下列第三方代码。分发二进制时必须
同时提供对应的许可证文本；`scripts/package.ps1` 会自动把 `License-Notepad4.txt`
（即上游 `License.txt`）与 `License-Scintilla.txt` 打进压缩包。

## Notepad4

- 版本：提交 `bc6c799223dadbaeabfc9f8125a6545cb162d64b`（2026-10-18），见 `upstream.lock`
- 来源：<https://github.com/zufuliu/notepad4>
- 许可证：BSD 3-Clause
- 版权：Notepad4 Copyright © 2011-2026 Zufu Liu and All contributors；
  Notepad2 Copyright © 2004-2012 Florian Balmer 等
- 完整文本：发布包内 `License-Notepad4.txt`

## Scintilla / Lexilla

- 随上游 Notepad4 一同编译（`notepad4/scintilla`、`notepad4/lexilla`）
- 许可证：Historical Permission Notice and Disclaimer (HPND)
- 来源：<https://www.scintilla.org/>
- 完整文本：发布包内 `License-Scintilla.txt`

## md4c

- 版本：0.5.2（`third_party/md4c`）
- 用途：Markdown 解析（CommonMark + GFM）
- 许可证：MIT
- 版权：Copyright © 2016-2024 Martin Mitáš
- 完整文本：`third_party/md4c/LICENSE.md`
- 本地改动：`md4c.c`、`entity.c` 中两个 `qsort` 比较函数显式标注 `__cdecl`
  （Notepad4 以 `/Gv` 编译，默认调用约定为 `__vectorcall`）。其余代码未修改。

## Microsoft Edge WebView2 SDK

- 版本：1.0.3351.48（`third_party/webview2`）
- 用途：预览页面的渲染宿主（`WebView2LoaderStatic.lib` 静态链接）
- 许可证：Microsoft 软件许可条款（随 SDK 分发）
- 来源：<https://www.nuget.org/packages/Microsoft.Web.WebView2>
- 说明：运行时依赖系统已安装的 WebView2 Runtime（Windows 11 自带，Windows 10
  由 Microsoft Edge 安装或单独分发）。
- 完整文本：`third_party/webview2/LICENSE.txt`

## highlight.js

- 版本：11.11.1（`preview/res/highlight.min.js`）
- 用途：代码块语法高亮，本地文件，不加载任何 CDN
- 许可证：BSD 3-Clause
- 版权：Copyright © 2006 Ivan Sagalaev，Copyright © 2018 Josh Goebel
- 完整文本：`third_party/highlightjs-LICENSE.txt`
