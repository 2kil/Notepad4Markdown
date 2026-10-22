// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - 内容服务层实现
//
#include "LocalContent.h"

#include "text_encoding.h"

#include <cstdio>

#include <algorithm>
#include <exception>
#include <new>
#include <filesystem>
#include <fstream>
#include <vector>

namespace mdpreview {

namespace {

constexpr size_t kMaxAssetBytes = 24u * 1024u * 1024u;

const wchar_t kTokenCsp[] = L"<!--MD_CSP-->";
const wchar_t kTokenStyle[] = L"<!--MD_STYLE-->";
const wchar_t kTokenScript[] = L"<!--MD_SCRIPT-->";
const wchar_t kTokenContent[] = L"<!--MD_CONTENT-->";

const char kLiveCsp[] =
	"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
	"script-src https://notepad4.mdpreview; "
	"style-src https://notepad4.mdpreview 'unsafe-inline'; "
	"img-src https://notepad4.mdpreview data: http: https:; "
	"font-src https://notepad4.mdpreview; connect-src https://notepad4.mdpreview; "
	"base-uri 'none'; form-action 'none'; frame-src 'none'; object-src 'none'\">";

const char kPlaceholder[] = "<p class=\"placeholder\">正在载入预览…</p>";

bool ReadWholeFile(const std::wstring& path, std::string& out) {
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec || size > kMaxAssetBytes) {
		return false;
	}
	std::ifstream stream(std::filesystem::path(path), std::ios::binary);
	if (!stream) {
		return false;
	}
	out.resize(static_cast<size_t>(size));
	if (size > 0) {
		stream.read(out.data(), static_cast<std::streamsize>(size));
		if (!stream) {
			out.clear();
			return false;
		}
	}
	return true;
}

// 打开文件后取最终规范路径再判断是否仍在文档目录内。
// 只做字符串比较时，".. "（Win32 会去掉尾随空格）或 junction 都能绕过。
bool IsPathInside(const std::wstring& base, const std::wstring& candidate);

bool ReadWholeFileWithin(const std::wstring& baseNormalized, const std::wstring& candidate,
	std::string& out, DWORD& win32Error) {
	win32Error = ERROR_SUCCESS;
	HANDLE file = ::CreateFileW(candidate.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		win32Error = ::GetLastError();
		return false;
	}

	wchar_t finalPath[32768] = {};
	const DWORD length = ::GetFinalPathNameByHandleW(file, finalPath, ARRAYSIZE(finalPath),
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (length == 0 || length >= ARRAYSIZE(finalPath)) {
		::CloseHandle(file);
		win32Error = ERROR_PATH_NOT_FOUND;
		return false;
	}

	std::wstring resolved(finalPath, length);
	// 去掉 "\\?\" 与 "\\?\UNC\" 前缀，便于与 GetFullPathNameW 的结果比较。
	const std::wstring uncPrefix = L"\\\\?\\UNC\\";
	const std::wstring dosPrefix = L"\\\\?\\";
	if (resolved.rfind(uncPrefix, 0) == 0) {
		resolved = L"\\\\" + resolved.substr(uncPrefix.size());
	} else if (resolved.rfind(dosPrefix, 0) == 0) {
		resolved = resolved.substr(dosPrefix.size());
	}

	if (!IsPathInside(baseNormalized, resolved)) {
		::CloseHandle(file);
		win32Error = ERROR_ACCESS_DENIED;
		return false;
	}

	LARGE_INTEGER size = {};
	if (!::GetFileSizeEx(file, &size)) {
		::CloseHandle(file);
		win32Error = ::GetLastError();
		return false;
	}
	if (size.QuadPart <= 0 || static_cast<unsigned long long>(size.QuadPart) > kMaxAssetBytes) {
		::CloseHandle(file);
		win32Error = ERROR_FILE_TOO_LARGE;
		return false;
	}

	out.resize(static_cast<size_t>(size.QuadPart));
	DWORD read = 0;
	const BOOL ok = ::ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
	::CloseHandle(file);
	if (!ok || read != out.size()) {
		out.clear();
		win32Error = ERROR_READ_FAULT;
		return false;
	}
	return true;
}

bool WriteWholeFile(const std::wstring& path, std::string_view data, std::string& error) {
	std::ofstream stream(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
	if (!stream) {
		error = "无法写入快照文件";
		return false;
	}
	stream.write(data.data(), static_cast<std::streamsize>(data.size()));
	if (!stream) {
		error = "快照写入过程中失败";
		return false;
	}
	return true;
}

std::wstring ReplaceAll(std::wstring text, const std::wstring& from, const std::wstring& to) {
	if (from.empty()) {
		return text;
	}
	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::wstring::npos) {
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
	return text;
}

// 百分号解码；非法序列按原样保留。
std::string PercentDecode(std::string_view input) {
	std::string out;
	out.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '%' && i + 2 < input.size()) {
			auto hex = [](char ch) -> int {
				if (ch >= '0' && ch <= '9') return ch - '0';
				if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
				if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
				return -1;
			};
			const int hi = hex(input[i + 1]);
			const int lo = hex(input[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(input[i]);
	}
	return out;
}

std::wstring LowerCase(std::wstring text) {
	for (wchar_t& ch : text) {
		if (ch >= L'A' && ch <= L'Z') {
			ch = static_cast<wchar_t>(ch - L'A' + L'a');
		}
	}
	return text;
}

std::wstring NormalizePath(const std::wstring& path) {
	wchar_t buffer[32768];
	const DWORD length = ::GetFullPathNameW(path.c_str(), ARRAYSIZE(buffer), buffer, nullptr);
	if (length == 0 || length >= ARRAYSIZE(buffer)) {
		return {};
	}
	std::wstring result(buffer, length);
	while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
		result.pop_back();
	}
	return result;
}

bool IsPathInside(const std::wstring& base, const std::wstring& candidate) {
	if (base.empty() || candidate.empty()) {
		return false;
	}
	const std::wstring lowerBase = LowerCase(base);
	const std::wstring lowerCandidate = LowerCase(candidate);
	if (lowerCandidate.size() <= lowerBase.size()) {
		return false;
	}
	if (lowerCandidate.compare(0, lowerBase.size(), lowerBase) != 0) {
		return false;
	}
	return lowerCandidate[lowerBase.size()] == L'\\';
}

const wchar_t* ContentTypeForExtension(std::wstring_view extension) {
	static const struct {
		const wchar_t* ext;
		const wchar_t* type;
	} table[] = {
		{ L".png", L"image/png" },
		{ L".jpg", L"image/jpeg" },
		{ L".jpeg", L"image/jpeg" },
		{ L".jfif", L"image/jpeg" },
		{ L".gif", L"image/gif" },
		{ L".bmp", L"image/bmp" },
		{ L".webp", L"image/webp" },
		{ L".avif", L"image/avif" },
		{ L".svg", L"image/svg+xml" },
		{ L".ico", L"image/x-icon" },
	};
	const std::wstring lower = LowerCase(std::wstring(extension));
	for (const auto& entry : table) {
		if (lower == entry.ext) {
			return entry.type;
		}
	}
	return nullptr;
}

std::string HtmlEscape(std::string_view text) {
	std::string out;
	out.reserve(text.size() + 16);
	for (const char ch : text) {
		switch (ch) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		default: out.push_back(ch); break;
		}
	}
	return out;
}

// JSON 字符串转义（只用于 /__meta，不处理对象/数组）。
std::string JsonEscapeBody(std::string_view text) {
	std::string out;
	out.reserve(text.size() + 8);
	for (const unsigned char ch : text) {
		switch (ch) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (ch < 0x20) {
				char buffer[8] = {};
				snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
				out += buffer;
			} else {
				out.push_back(static_cast<char>(ch));
			}
			break;
		}
	}
	return out;
}

std::string BuildMetaJson(const DocMeta& meta, int64_t version) {
	std::string out = "{";
	out += "\"version\":" + std::to_string(version);
	out += ",\"path\":\"" + JsonEscapeBody(meta.path) + "\"";
	out += ",\"dir\":\"" + JsonEscapeBody(meta.dir) + "\"";
	out += ",\"title\":\"" + JsonEscapeBody(meta.title) + "\"";
	out += ",\"encoding\":\"" + JsonEscapeBody(meta.encoding) + "\"";
	out += ",\"modified\":" + std::string(meta.modified ? "true" : "false");
	out += ",\"error\":\"" + JsonEscapeBody(meta.error) + "\"";
	out += "}";
	return out;
}

}  // namespace

void LocalContent::SetAppDir(std::wstring appDir) {
	std::lock_guard<std::mutex> lock(configMutex_);
	appDir_ = std::move(appDir);
}

void LocalContent::SetModule(HINSTANCE instance) noexcept {
	std::lock_guard<std::mutex> lock(configMutex_);
	module_ = instance;
}

// 资源名 → RCDATA ID。与 notepad4/src/resource.h 的 IDR_MDPREVIEW_* 一一对应。
int AssetIdFromName(std::wstring_view name) {
	if (name == L"shell.html") {
		return kAssetShell;
	}
	if (name == L"app.css") {
		return kAssetCss;
	}
	if (name == L"app.js") {
		return kAssetJs;
	}
	if (name == L"highlight.min.js") {
		return kAssetHighlight;
	}
	return 0;
}

bool LocalContent::LoadAsset(std::wstring_view name, std::string& body) const {
	std::wstring dir;
	HINSTANCE module = nullptr;
	{
		std::lock_guard<std::mutex> lock(configMutex_);
		dir = appDir_;
		module = module_;
	}

	// 1) 磁盘文件优先：开发时可以直接改 CSS/JS，不必重新编译。
	if (!dir.empty()) {
		std::wstring path = dir;
		if (path.back() != L'\\') {
			path.push_back(L'\\');
		}
		path.append(name);
		if (ReadWholeFile(path, body)) {
			return true;
		}
	}

	// 2) 回退到编译进 exe 的 RCDATA。
	if (module == nullptr) {
		return false;
	}
	const int id = AssetIdFromName(name);
	if (id == 0) {
		return false;
	}
	const HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
	if (resource == nullptr) {
		return false;
	}
	const HGLOBAL handle = ::LoadResource(module, resource);
	if (handle == nullptr) {
		return false;
	}
	const void* data = ::LockResource(handle);
	const DWORD size = ::SizeofResource(module, resource);
	if (data == nullptr || size == 0) {
		return false;
	}
	body.assign(static_cast<const char*>(data), size);
	return true;
}

std::wstring LocalContent::AppDir() const {
	std::lock_guard<std::mutex> lock(configMutex_);
	return appDir_;
}

RenderResult LocalContent::UpdateDocument(std::string markdownUtf8, DocMeta meta) {
	RenderOptions options;
	options.baseDir = meta.dir;

	RenderResult result;
	try {
		result = RenderMarkdown(markdownUtf8, options);
	} catch (const std::bad_alloc&) {
		// 32 MB 上限下仍可能因多份副本叠加而分配失败；
		// 不能让异常越过工作线程边界（会直接终止进程）。
		result.ok = false;
		result.error = "内存不足，渲染已中止";
	} catch (const std::exception&) {
		result.ok = false;
		result.error = "渲染过程中发生异常";
	}
	if (!result.ok && result.error.empty()) {
		result.error = "未知渲染错误";
	}

	std::lock_guard<std::mutex> lock(mutex_);
	html_ = result.ok ? result.html : std::string();
	meta_ = std::move(meta);
	meta_.error = result.ok ? std::string() : result.error;
	version_ += 1;
	return result;
}

void LocalContent::SetError(std::string message, DocMeta meta) {
	std::lock_guard<std::mutex> lock(mutex_);
	html_.clear();
	meta_ = std::move(meta);
	meta_.error = std::move(message);
	version_ += 1;
}

void LocalContent::SetTheme(std::string mode) {
	std::lock_guard<std::mutex> lock(mutex_);
	theme_ = std::move(mode);
}

std::string LocalContent::Theme() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return theme_;
}

std::string LocalContent::MetaJson() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return BuildMetaJson(meta_, version_);
}

DocMeta LocalContent::Meta() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return meta_;
}

int64_t LocalContent::Version() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return version_;
}

HttpResponse LocalContent::Serve(std::wstring_view pathAndQuery) const {
	HttpResponse response;

	// 剥离查询串
	std::wstring path(pathAndQuery);
	const size_t queryPos = path.find_first_of(L"?#");
	if (queryPos != std::wstring::npos) {
		path.erase(queryPos);
	}

	if (path == L"/__doc") {
		std::lock_guard<std::mutex> lock(mutex_);
		response.status = 200;
		response.contentType = L"text/html; charset=utf-8";
		response.body = html_;
		if (html_.empty() && !meta_.error.empty()) {
			response.body = "<p class=\"placeholder\">无法解析该文档。</p>";
		}
		return response;
	}

	if (path == L"/__meta") {
		response.status = 200;
		response.contentType = L"application/json; charset=utf-8";
		response.body = MetaJson();
		return response;
	}

	// 根路径与 /__app/shell.html 都返回页面框架。用根路径加载时，
	// 文档里的相对图片（./static/a.png）才会解析为 /static/a.png。
	if (path == kShellPath || path == L"/" || path.empty()) {
		std::string shell;
		if (!LoadAsset(L"shell.html", shell)) {
			response.status = 500;
			response.body = "预览资源缺失";
			return response;
		}
		std::wstring wide = Utf8ToWide(shell);
		wide = ReplaceAll(std::move(wide), kTokenCsp, Utf8ToWide(kLiveCsp));
		wide = ReplaceAll(std::move(wide), kTokenStyle,
			L"<link rel=\"stylesheet\" href=\"/__app/app.css\">");
		wide = ReplaceAll(std::move(wide), kTokenScript,
			L"<script src=\"/__app/highlight.min.js\"></script>\n"
			L"<script src=\"/__app/app.js\"></script>");
		wide = ReplaceAll(std::move(wide), kTokenContent, Utf8ToWide(kPlaceholder));
		response.status = 200;
		response.contentType = L"text/html; charset=utf-8";
		response.body = WideToUtf8(wide);
		return response;
	}

	// 内置静态资源
	if (path.rfind(L"/__app/", 0) == 0) {
		const std::wstring name = path.substr(7);
		std::string body;
		if (name == L"app.css" || name == L"app.js" || name == L"highlight.min.js") {
			if (LoadAsset(name, body)) {
				response.status = 200;
				response.contentType = (name == L"app.css") ? L"text/css; charset=utf-8"
					: L"application/javascript; charset=utf-8";
				response.body = std::move(body);
				return response;
			}
		}
		response.status = 404;
		response.body = "资源不存在";
		return response;
	}

	// 文档相对资源：只允许图片，且必须位于文档目录内
	std::wstring dir;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir = Utf8ToWide(meta_.dir);
	}
	if (dir.empty()) {
		response.status = 404;
		response.body = "文档未保存，无法解析相对路径";
		return response;
	}

	std::wstring relative = path;
	while (!relative.empty() && (relative.front() == L'/' || relative.front() == L'\\')) {
		relative.erase(relative.begin());
	}
	relative = Utf8ToWide(PercentDecode(WideToUtf8(relative)));
	if (relative.empty()) {
		response.status = 404;
		return response;
	}
	// 拒绝绝对路径、盘符与上跳路径
	if (relative.find(L':') != std::wstring::npos
		|| relative.compare(0, 2, L"\\\\") == 0) {
		response.status = 403;
		response.body = "拒绝访问";
		return response;
	}
	{
		size_t start = 0;
		while (start <= relative.size()) {
			size_t end = relative.find_first_of(L"\\/", start);
			if (end == std::wstring::npos) {
				end = relative.size();
			}
			const std::wstring segment = relative.substr(start, end - start);
			// Win32 会去掉路径段末尾的空格与点，因此 ".. " 等价于 ".."；
			// 这类变体必须在字符串层面就拒绝。
			std::wstring trimmed = segment;
			while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t'
				|| trimmed.back() == L'.')) {
				trimmed.pop_back();
			}
			if (trimmed == L".." || trimmed == L"."
				|| (!segment.empty() && (segment.back() == L' ' || segment.back() == L'\t'))) {
				response.status = 403;
				response.body = "拒绝访问";
				return response;
			}
			if (end >= relative.size()) {
				break;
			}
			start = end + 1;
		}
	}

	const size_t dot = relative.find_last_of(L'.');
	if (dot == std::wstring::npos) {
		response.status = 404;
		return response;
	}
	const wchar_t* contentType = ContentTypeForExtension(relative.substr(dot));
	if (contentType == nullptr) {
		response.status = 403;
		response.body = "不允许的资源类型";
		return response;
	}

	const std::wstring baseNormalized = NormalizePath(dir);
	std::wstring full = baseNormalized + L"\\" + relative;
	full = NormalizePath(full);
	if (!IsPathInside(baseNormalized, full)) {
		response.status = 403;
		response.body = "路径越界";
		return response;
	}

	std::string body;
	DWORD readError = ERROR_SUCCESS;
	if (!ReadWholeFileWithin(baseNormalized, full, body, readError)) {
		// 句柄级校验失败（越界/链接/超大）与单纯不存在要区分开。
		response.status = (readError == ERROR_ACCESS_DENIED || readError == ERROR_FILE_TOO_LARGE) ? 403 : 404;
		response.body = (response.status == 403) ? "拒绝访问" : "文件不存在或无法读取";
		return response;
	}
	response.status = 200;
	response.contentType = contentType;
	response.body = std::move(body);
	return response;
}

bool LocalContent::ExportSnapshot(const std::wstring& outPath, std::string& error) const {
	std::string shell;
	if (!LoadAsset(L"shell.html", shell)) {
		error = "预览资源缺失";
		return false;
	}

	std::string css;
	std::string js;
	std::string highlight;
	LoadAsset(L"app.css", css);
	LoadAsset(L"app.js", js);
	LoadAsset(L"highlight.min.js", highlight);

	std::string html;
	std::string title;
	std::string baseDir;
	std::string theme;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		html = html_;
		title = meta_.title;
		baseDir = meta_.dir;
		theme = theme_;
	}

	if (html.empty()) {
		error = "当前没有可导出的内容";
		return false;
	}

	std::string body = RewriteRelativeUrlsToFile(html, baseDir);

	static const char kSnapshotCsp[] =
		"<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
		"style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
		"img-src data: file: http: https:; base-uri 'none'; form-action 'none'\">";

	if (theme != "light" && theme != "dark") {
		theme = "auto";
	}

	std::string inlineScript = "<script>";
	inlineScript += highlight;
	inlineScript += "</script>\n<script>";
	inlineScript +=
		"(function(){try{var m=localStorage.getItem('mdpreview.snapshot.theme')||'";
	inlineScript += theme;
	inlineScript +=
		"';if(m==='light'||m==='dark'){document.documentElement.setAttribute('data-theme',m);}"
		"var b=document.getElementById('theme-toggle');if(b){b.addEventListener('click',function(){"
		"var c=document.documentElement.getAttribute('data-theme');"
		"var n=(c==='light')?'dark':'light';"
		"document.documentElement.setAttribute('data-theme',n);"
		"try{localStorage.setItem('mdpreview.snapshot.theme',n);}catch(e){}"
		"});}}catch(e){}"
		"if(window.hljs){var els=document.querySelectorAll('pre > code');for(var i=0;i<els.length;i++){"
		"if((els[i].textContent||'').length<200000){try{hljs.highlightElement(els[i]);}catch(e){}}}}"
		"})();</script>";

	std::wstring result = Utf8ToWide(shell);
	result = ReplaceAll(std::move(result), kTokenCsp, Utf8ToWide(kSnapshotCsp));
	result = ReplaceAll(std::move(result), kTokenStyle, L"<style>" + Utf8ToWide(css) + L"</style>");
	result = ReplaceAll(std::move(result), kTokenScript, Utf8ToWide(inlineScript));
	result = ReplaceAll(std::move(result), kTokenContent, Utf8ToWide(body));
	result = ReplaceAll(std::move(result), L"<title>Markdown 预览</title>",
		L"<title>" + Utf8ToWide(HtmlEscape(title)) + L"</title>");

	return WriteWholeFile(outPath, WideToUtf8(result), error);
}

}  // namespace mdpreview
