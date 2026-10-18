// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - 内容服务层
//
// 负责：
//   * 缓存当前文档的渲染结果；
//   * 响应 WebView2 的资源请求（仅限 https://notepad4.mdpreview/* 虚拟域）；
//   * 把相对路径解析到"文档目录白名单"内，越界一律 404；
//   * 导出独立 HTML 快照（用于"在浏览器中打开"）。

#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "MarkdownRenderer.h"

namespace mdpreview {

inline constexpr wchar_t kVirtualHost[] = L"notepad4.mdpreview";
inline constexpr wchar_t kShellPath[] = L"/__app/shell.html";

struct DocMeta {
	std::string path;       // UTF-8 绝对路径（未命名文档为空）
	std::string dir;        // UTF-8 目录（未命名文档为空）
	std::string title;
	std::string encoding;
	std::string error;      // 渲染或读取错误（展示给用户）
	int64_t version = 0;
	bool modified = false;
};

struct HttpResponse {
	int status = 404;
	std::wstring contentType = L"text/plain; charset=utf-8";
	std::string body;
};

// 内嵌资源 ID（与 notepad4/src/resource.h 中的 IDR_MDPREVIEW_* 保持一致）。
inline constexpr int kAssetShell = 210;
inline constexpr int kAssetCss = 211;
inline constexpr int kAssetJs = 212;
inline constexpr int kAssetHighlight = 213;

class LocalContent {
public:
	// 资源目录（preview/res）。存在同名文件时优先使用文件，便于开发时调整样式。
	void SetAppDir(std::wstring appDir);
	std::wstring AppDir() const;

	// 内嵌资源所在模块（通常是 Notepad4.exe 自身）。文件缺失时回退到此。
	void SetModule(HINSTANCE instance) noexcept;

	// 更新文档：完成渲染并缓存。返回渲染统计用于状态显示。
	RenderResult UpdateDocument(std::string markdownUtf8, DocMeta meta);

	// 直接把当前文档置为错误状态（例如体积超限），页面显示可读提示。
	void SetError(std::string message, DocMeta meta);

	void SetTheme(std::string mode);
	std::string Theme() const;

	// 处理虚拟域下的请求路径（含查询串，已剥离域名）。
	HttpResponse Serve(std::wstring_view pathAndQuery) const;

	std::string MetaJson() const;
	DocMeta Meta() const;
	int64_t Version() const;

	// 导出快照 HTML 到 outPath（覆盖写入）。
	bool ExportSnapshot(const std::wstring& outPath, std::string& error) const;

private:
	// 先读 <appDir>\<name>，失败则读模块内的 RCDATA。
	bool LoadAsset(std::wstring_view name, std::string& body) const;

	mutable std::mutex mutex_;          // 保护渲染结果与元信息
	mutable std::mutex configMutex_;    // 保护资源目录/模块句柄（避免与 mutex_ 嵌套）
	HINSTANCE module_ = nullptr;
	std::wstring appDir_;
	std::string html_;
	DocMeta meta_;
	std::string theme_ = "auto";
	int64_t version_ = 0;
};

}  // namespace mdpreview
