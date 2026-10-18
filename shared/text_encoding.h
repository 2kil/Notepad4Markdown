// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - 文本编码转换
//
// Scintilla 缓冲区可能是 UTF-8，也可能是当前代码页（ANSI）编码；
// 渲染层只接受 UTF-8，因此统一在这里转换。

#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace mdpreview {

// UTF-16 → UTF-8
std::string WideToUtf8(std::wstring_view text);

// UTF-8 → UTF-16
std::wstring Utf8ToWide(std::string_view text);

// 任意 Windows 代码页 → UTF-8；codePage 为 CP_UTF8/65001/0 时原样返回。
std::string ToUtf8(std::string_view bytes, UINT codePage);

}  // namespace mdpreview
