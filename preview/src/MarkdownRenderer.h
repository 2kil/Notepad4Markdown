// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - Markdown 渲染层
//
// 职责：
//   1. 调用 md4c（CommonMark + GFM 扩展）把 Markdown 编译为 HTML 片段。
//   2. 对结果做"安全后处理"：URL 白名单、原始 HTML 禁用、表格包裹、图片懒加载。
//
// 该层不依赖 Windows API，可被单元测试直接调用。

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mdpreview {

struct RenderStats {
	size_t links = 0;
	size_t images = 0;
	size_t tables = 0;
	size_t blockedUrls = 0;
	uint64_t elapsedMs = 0;
};

struct RenderOptions {
	// 文档所在目录（UTF-8）。用于生成图片/链接的"来源上下文"，
	// 以及后续资源解析的允许范围判断。
	std::string baseDir;
};

struct RenderResult {
	bool ok = false;
	std::string html;     // 完整 HTML 片段（不含 <html>/<body>）
	std::string error;
	RenderStats stats;
};

// 渲染入口。输入为 UTF-8 Markdown 正文。
RenderResult RenderMarkdown(std::string_view markdownUtf8, const RenderOptions& options);

// 供测试与快照导出复用的工具函数。
//
// 把渲染结果中的"相对 URL"重写为 file:/// 绝对 URL，用于导出独立 HTML 快照。
// baseDir 必须为 Windows 绝对路径（UTF-8）。无法解析或越界的 URL 会被替换为占位符。
std::string RewriteRelativeUrlsToFile(std::string_view html, std::string_view baseDir);

// URL 安全判定（导出给测试使用）。
enum class UrlUse { Link, Image };
bool IsUrlAllowed(std::string_view url, UrlUse use, std::string& reason);

}  // namespace mdpreview
