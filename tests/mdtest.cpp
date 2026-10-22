// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - 自动化测试
//
// 覆盖：IPC 协议编解码、Markdown 渲染、安全策略、资源路由白名单、快照 URL 改写。
// 运行：build\out\x64\mdtest.exe  （由 scripts\run_tests.cmd 调用）
//
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "LocalContent.h"
#include "MarkdownRenderer.h"
#include "text_encoding.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char* name, const std::string& detail = {}) {
	++g_checks;
	if (condition) {
		printf("  PASS  %s\n", name);
	} else {
		++g_failures;
		printf("  FAIL  %s", name);
		if (!detail.empty()) {
			printf("  [%s]", detail.c_str());
		}
		printf("\n");
	}
}

std::string WideToUtf8ForTest(const std::wstring& text) {
	return mdpreview::WideToUtf8(text);
}

void CheckContains(const std::string& haystack, const std::string& needle, const char* name) {
	Check(haystack.find(needle) != std::string::npos, name, needle);
}

void CheckNotContains(const std::string& haystack, const std::string& needle, const char* name) {
	Check(haystack.find(needle) == std::string::npos, name, needle);
}

std::wstring ExeDir() {
	wchar_t buffer[MAX_PATH * 4];
	const DWORD length = ::GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
	std::wstring path(buffer, length);
	const size_t slash = path.find_last_of(L"\\/");
	return (slash == std::wstring::npos) ? path : path.substr(0, slash);
}

std::wstring AbsolutePath(const std::wstring& relative) {
	wchar_t buffer[MAX_PATH * 4];
	if (::GetFullPathNameW(relative.c_str(), ARRAYSIZE(buffer), buffer, nullptr) == 0) {
		return relative;
	}
	return buffer;
}

// ---------------------------------------------------------------------------
// 1. 协议
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// 2. 渲染
// ---------------------------------------------------------------------------

void TestRendering() {
	printf("[Markdown 渲染]\n");

	mdpreview::RenderOptions options;
	options.baseDir = "C:\\docs";

	struct Case {
		const char* name;
		const char* markdown;
		const char* expected;
	};
	const Case cases[] = {
		{ "ATX 标题", "# 一级\n## 二级\n", "<h1 id=\"一级\">一级</h1>" },
		{ "段落", "普通段落文本\n", "<p>普通段落文本</p>" },
		{ "粗体", "**粗体**\n", "<strong>粗体</strong>" },
		{ "斜体", "*斜体*\n", "<em>斜体</em>" },
		{ "删除线", "~~删除~~\n", "<del>删除</del>" },
		{ "行内代码", "`code`\n", "<code>code</code>" },
		{ "引用", "> 引用\n", "<blockquote>" },
		{ "分隔线", "---\n", "<hr>" },
		{ "无序列表", "- a\n- b\n", "<ul>" },
		{ "有序列表", "1. a\n2. b\n", "<ol>" },
		{ "任务列表", "- [x] done\n", "task-list-item" },
		{ "代码块语言", "```cpp\nint a;\n```\n", "language-cpp" },
		{ "表格", "| a | b |\n| --- | --- |\n| 1 | 2 |\n", "<table>" },
		{ "自动链接", "见 https://example.com 。\n", "<a href=\"https://example.com\">" },
		{ "HTML 实体", "&amp; &lt;\n", "&amp;" },
	};

	for (const auto& item : cases) {
		const mdpreview::RenderResult result = mdpreview::RenderMarkdown(item.markdown, options);
		Check(result.ok, item.name);
		if (result.ok) {
			CheckContains(result.html, item.expected, item.name);
		}
	}

	// 表格包裹容器（横向滚动）
	const mdpreview::RenderResult table = mdpreview::RenderMarkdown(
		"| a | b |\n| --- | --- |\n| 1 | 2 |\n", options);
	CheckContains(table.html, "<div class=\"md-table\">", "表格被包裹为可横向滚动容器");
	Check(table.stats.tables == 1, "表格计数正确");

	// 图片：本地相对路径 / 远程 / data URI 都要保留，file: 绝对路径要拒绝
	const mdpreview::RenderResult image = mdpreview::RenderMarkdown("![x](a.png)\n", options);
	// 刻意不使用 loading="lazy"：面板被遮挡时 Chromium 会一直推迟加载。
	CheckNotContains(image.html, "loading=\"lazy\"", "图片不使用懒加载（避免遮挡时不加载）");
	CheckContains(image.html, "decoding=\"async\"", "图片带 async 解码提示");
	CheckContains(image.html, "src=\"a.png\"", "相对路径图片保留原样（由路由层解析）");

	const mdpreview::RenderResult remote = mdpreview::RenderMarkdown(
		"![r](https://example.com/a.png)\n\n![d](data:image/png;base64,iVBORw0KGgo=)\n", options);
	CheckContains(remote.html, "src=\"https://example.com/a.png\"", "网络图片地址被保留");
	CheckContains(remote.html, "src=\"data:image/png;base64", "data:image 图片被保留");
	Check(remote.stats.images == 2 && remote.stats.blockedUrls == 0, "两张图片均未拦截");

	const mdpreview::RenderResult fileImage = mdpreview::RenderMarkdown(
		"![f](file:///C:/Windows/win.ini)\n", options);
	CheckNotContains(fileImage.html, "file:///C:/Windows", "文档中的 file: 绝对路径被拒绝");
	Check(fileImage.stats.blockedUrls == 1, "file: 绝对路径计入拦截统计");

	// 原始 HTML：安全标签保留（README 排版依赖），危险标签连同内容丢弃
	const mdpreview::RenderResult raw = mdpreview::RenderMarkdown(
		"<script>alert(1)</script>\n\n<b>bold</b> and <img src=x onerror=\"alert(2)\">\n", options);
	CheckNotContains(raw.html, "<script", "<script> 标签被移除");
	CheckNotContains(raw.html, "alert(1)", "<script> 内容未作为文本漏出");
	CheckContains(raw.html, "<b>bold</b>", "安全的内联 HTML 标签被保留");
	CheckNotContains(raw.html, "onerror", "事件属性被移除");

	// 常见 README 排版：居中容器、details、图片
	const mdpreview::RenderResult layout = mdpreview::RenderMarkdown(
		"<div align=\"center\">\n<img src=\"a.png\" width=\"120\" alt=\"logo\"><br>\n"
		"<h1>Title</h1>\n</div>\n\n<details><summary>more</summary>\n\n- item\n\n</details>\n",
		options);
	CheckContains(layout.html, "<div align=\"center\">", "div + align 保留");
	CheckContains(layout.html, "width=\"120\"", "图片尺寸属性保留");
	CheckContains(layout.html, "<details>", "details 保留");
	CheckContains(layout.html, "<summary>", "summary 保留");
	CheckNotContains(layout.html, "&lt;div", "HTML 不再被转义为文本");

	// 危险协议 / 样式注入
	const mdpreview::RenderResult evil = mdpreview::RenderMarkdown(
		"<a href=\"javascript:alert(1)\">x</a>\n\n"
		"<img src=\"javascript:alert(2)\">\n\n"
		"<div style=\"background:url(javascript:alert(3))\">y</div>\n\n"
		"<iframe src=\"https://example.com\"></iframe>\n", options);
	CheckNotContains(evil.html, "javascript:", "原始 HTML 中的 javascript: 协议被移除");
	CheckNotContains(evil.html, "<iframe", "iframe 被移除");

	// 中文与 Unicode
	const mdpreview::RenderResult cjk = mdpreview::RenderMarkdown("# 中文标题 🎉\n\n汉字、かな、한글。\n", options);
	CheckContains(cjk.html, "中文标题 🎉", "中文与 emoji 原样保留");
	CheckContains(cjk.html, "한글", "韩文保留");

	// 长文档
	std::string longDoc;
	longDoc.reserve(256 * 1024);
	for (int i = 0; i < 2000; ++i) {
		longDoc += "## 章节 ";
		longDoc += std::to_string(i);
		longDoc += "\n\n这是一段用于长文档渲染测试的中文段落。\n\n";
	}
	const mdpreview::RenderResult longResult = mdpreview::RenderMarkdown(longDoc, options);
	Check(longResult.ok && longResult.html.size() > longDoc.size() / 2, "长文档渲染成功");
	printf("        (长文档 %zu 字节, 渲染耗时 %llu ms)\n", longDoc.size(),
		static_cast<unsigned long long>(longResult.stats.elapsedMs));
}

// ---------------------------------------------------------------------------
// 3. 安全
// ---------------------------------------------------------------------------

void TestSecurity() {
	printf("[安全策略]\n");

	mdpreview::RenderOptions options;
	options.baseDir = "C:\\docs";

	const mdpreview::RenderResult result = mdpreview::RenderMarkdown(
		"[a](javascript:alert(1))\n\n"
		"[b](java\tscript:alert(2))\n\n"
		"[c](data:text/html;base64,PHNjcmlwdD4=)\n\n"
		"[d](//evil.example.com/x)\n\n"
		"[e](file:///C:/Windows/win.ini)\n\n"
		"[ok](https://example.com)\n\n"
		"[mail](mailto:a@b.c)\n\n"
		"![bad](javascript:alert(3))\n\n"
		"![good](data:image/png;base64,iVBORw0KGgo=)\n",
		options);

	CheckNotContains(result.html, "javascript:", "javascript: 链接被拦截");
	CheckNotContains(result.html, "data:text/html", "data:text/html 链接被拦截");
	CheckNotContains(result.html, "file:///C:/Windows", "file: 链接被拦截");
	CheckNotContains(result.html, "evil.example.com", "协议相对 URL 被拦截");
	CheckContains(result.html, "href=\"#md-blocked-link\"", "被拦截链接替换为占位锚点");
	CheckContains(result.html, "href=\"https://example.com\"", "https 链接保留");
	CheckContains(result.html, "href=\"mailto:a@b.c\"", "mailto 链接保留");
	CheckContains(result.html, "data:image/png;base64", "data:image 图片保留");
	Check(result.stats.blockedUrls >= 4, "拦截计数正确");

	std::string reason;
	Check(!mdpreview::IsUrlAllowed("vbscript:msgbox(1)", mdpreview::UrlUse::Link, reason), "vbscript 被拒绝");
	Check(!mdpreview::IsUrlAllowed("  javascript:alert(1)  ", mdpreview::UrlUse::Link, reason), "首尾空白的 javascript 被拒绝");
	Check(!mdpreview::IsUrlAllowed("java\nscript:alert(1)", mdpreview::UrlUse::Link, reason), "含换行的 javascript 被拒绝");
	Check(mdpreview::IsUrlAllowed("images/a.png", mdpreview::UrlUse::Image, reason), "相对图片路径允许");

	// --- 回归：原始 HTML 的边界与绕过 ---
	// 未闭合的 <script src=...> 曾把后面的全部正文吞掉。
	const mdpreview::RenderResult unclosed = mdpreview::RenderMarkdown(
		"<script src=\"x.js\">\n\n## 标题\n\n正文必须保留\n", options);
	CheckContains(unclosed.html, "正文必须保留", "未闭合 script 不再吞掉后续正文");
	CheckNotContains(unclosed.html, "<script", "未闭合 script 标签本身被移除");

	// loading="lazy" 曾让被遮挡的面板长期不加载图片。
	const mdpreview::RenderResult lazy = mdpreview::RenderMarkdown(
		"<img src=\"a.png\" loading=\"lazy\">\n", options);
	CheckNotContains(lazy.html, "loading=", "loading 属性被丢弃");

	// srcset 曾整串只校验开头，后面的候选可以夹带 javascript:。
	const mdpreview::RenderResult srcset = mdpreview::RenderMarkdown(
		"<img src=\"a.png\" srcset=\"b.png 1x, javascript:alert(1) 2x\">\n", options);
	CheckNotContains(srcset.html, "javascript:alert(1)", "srcset 中的危险候选被拒绝");
	const mdpreview::RenderResult srcsetOk = mdpreview::RenderMarkdown(
		"<img src=\"a.png\" srcset=\"b.png 1x, c.png 2x\">\n", options);
	CheckContains(srcsetOk.html, "srcset=\"b.png 1x, c.png 2x\"", "正常的 srcset 保留");

	// 目录跳转：标题必须带 GitHub 风格 id，否则 [文字](#锚点) 点了没反应。
	const mdpreview::RenderResult toc = mdpreview::RenderMarkdown(
		"- [0. 基础信息](#0-基础信息)\n- [2. 直播伴侣：开播下播](#2-直播伴侣开播下播)\n\n"
		"---\n\n## 0. 基础信息\n\n## 2. 直播伴侣：开播下播\n", options);
	CheckContains(toc.html, "<h2 id=\"0-基础信息\">", "中文标题生成锚点 id");
	CheckContains(toc.html, "<h2 id=\"2-直播伴侣开播下播\">", "全角冒号在锚点中被丢弃");
	Check(toc.stats.headings == 2, "标题计数正确");

	// 重复标题与纯符号标题也要有唯一、非空的锚点。
	const mdpreview::RenderResult dup = mdpreview::RenderMarkdown(
		"## 说明\n\n## 说明\n\n## !!!\n", options);
	CheckContains(dup.html, "id=\"说明\"", "首个同名标题保留原名");
	CheckContains(dup.html, "id=\"说明-1\"", "同名标题追加序号");
	CheckContains(dup.html, "id=\"section\"", "纯符号标题回退为 section");

	// 标题里的行内标记不应出现在锚点里。
	const mdpreview::RenderResult code = mdpreview::RenderMarkdown(
		"## 用 `git apply` 打补丁\n", options);
	CheckContains(code.html, "id=\"用-git-apply-打补丁\"", "锚点忽略行内代码的反引号");

	// 属性值里的 '>' 曾让标签在中间被截断。
	const mdpreview::RenderResult quote = mdpreview::RenderMarkdown(
		"<div title=\"a>b\">inner</div>\n", options);
	CheckContains(quote.html, "title=\"a>b\"", "属性值中的 > 不再截断标签");
	CheckContains(quote.html, "inner", "标签后的内容保留");
	Check(mdpreview::IsUrlAllowed("#anchor", mdpreview::UrlUse::Link, reason), "页内锚点允许");
}

// ---------------------------------------------------------------------------
// 4. 资源路由白名单
// ---------------------------------------------------------------------------

void TestAssetRouter(const std::wstring& appDir, const std::wstring& docDir) {
	printf("[资源路由]\n");

	mdpreview::LocalContent content;
	content.SetAppDir(appDir);

	mdpreview::DocMeta meta;
	meta.dir = mdpreview::WideToUtf8(docDir);
	meta.title = "basic.md";
	meta.path = meta.dir + "\\basic.md";
	content.UpdateDocument("# hello\n\n![x](img/pic.png)\n", meta);

	const mdpreview::HttpResponse doc = content.Serve(L"/__doc");
	Check(doc.status == 200 && doc.body.find("<h1 id=\"hello\">hello</h1>") != std::string::npos,
		"GET /__doc 返回渲染结果");

	const mdpreview::HttpResponse metaResponse = content.Serve(L"/__meta");
	Check(metaResponse.status == 200 && metaResponse.body.find("\"title\"") != std::string::npos,
		"GET /__meta 返回 JSON");

	const mdpreview::HttpResponse css = content.Serve(L"/__app/app.css");
	Check(css.status == 200 && css.contentType.find(L"text/css") == 0, "GET /__app/app.css 返回样式",
		std::to_string(css.status));

	const mdpreview::HttpResponse shell = content.Serve(L"/__app/shell.html");
	Check(shell.status == 200 && shell.body.find("/__app/app.js") != std::string::npos,
		"GET /__app/shell.html 注入页面脚本", std::to_string(shell.status));

	const mdpreview::HttpResponse asset = content.Serve(L"/img/pic.png");
	Check(asset.status == 200 && asset.body.size() > 8 && asset.contentType == L"image/png",
		"文档相对图片可读取");

	const mdpreview::HttpResponse missing = content.Serve(L"/img/none.png");
	Check(missing.status == 404, "不存在的图片返回 404");

	const mdpreview::HttpResponse escape1 = content.Serve(L"/../secret.txt");
	Check(escape1.status == 403 || escape1.status == 404, "上跳路径被拒绝");

	const mdpreview::HttpResponse escape2 = content.Serve(L"/%2e%2e/secret.txt");
	Check(escape2.status == 403 || escape2.status == 404, "百分号编码的上跳路径被拒绝");

	const mdpreview::HttpResponse escape3 = content.Serve(L"/C:/Windows/win.ini");
	Check(escape3.status == 403 || escape3.status == 404, "绝对路径被拒绝");

	// Win32 会去掉路径段末尾的空格与点，".. " 实际等价于 ".."。
	const mdpreview::HttpResponse escape4 = content.Serve(L"/.. /secret.txt");
	Check(escape4.status == 403 || escape4.status == 404, "尾随空格的 .. 被拒绝");
	const mdpreview::HttpResponse escape5 = content.Serve(L"/..%20/secret.txt");
	Check(escape5.status == 403 || escape5.status == 404, "编码后的尾随空格 .. 被拒绝");
	const mdpreview::HttpResponse escape6 = content.Serve(L"/. ./secret.txt");
	Check(escape6.status == 403 || escape6.status == 404, "点与空格变体被拒绝");

	const mdpreview::HttpResponse nonImage = content.Serve(L"/secret.txt");
	Check(nonImage.status == 403, "非图片扩展名被拒绝");

	// 未保存文档（无目录）不允许任何相对资源
	mdpreview::LocalContent unsaved;
	unsaved.SetAppDir(appDir);
	mdpreview::DocMeta unsavedMeta;
	unsavedMeta.title = "未命名文档";
	unsaved.UpdateDocument("# x\n\n![y](img/pic.png)\n", unsavedMeta);
	const mdpreview::HttpResponse noDir = unsaved.Serve(L"/img/pic.png");
	Check(noDir.status == 404, "未保存文档的相对资源被拒绝");

	// 版本号单调递增
	const int64_t before = content.Version();
	content.UpdateDocument("# 2\n", meta);
	Check(content.Version() == before + 1, "文档版本号单调递增");
}

// ---------------------------------------------------------------------------
// 5. 快照 URL 改写
// ---------------------------------------------------------------------------

void TestSnapshotRewrite() {
	printf("[快照导出]\n");

	const std::string html = "<p><img src=\"img/a.png\" alt=\"a\"><img src=\"https://cdn.example.com/b.png\"></p>";
	const std::string rewritten = mdpreview::RewriteRelativeUrlsToFile(html, "C:\\docs");
	CheckContains(rewritten, "src=\"file:///C:/docs/img/a.png\"", "相对图片改写为 file:/// 绝对路径");
	CheckContains(rewritten, "src=\"https://cdn.example.com/b.png\"", "远程图片保持不变");

	const std::string escaped = mdpreview::RewriteRelativeUrlsToFile(
		"<p><img src=\"../secret.png\"></p>", "C:\\docs\\sub");
	CheckContains(escaped, "data:image/gif", "越界相对路径替换为占位图");
}

}  // namespace

std::string ReadAllBytes(const std::wstring& path) {
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	LARGE_INTEGER size = {};
	::GetFileSizeEx(file, &size);
	std::string content(static_cast<size_t>(size.QuadPart), '\0');
	DWORD read = 0;
	::ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr);
	::CloseHandle(file);
	content.resize(read);
	return content;
}

// --render <in.md> <out.html>：把 Markdown 渲染成可独立打开的 HTML，便于排查排版问题。
int RenderToFile(const std::wstring& input, const std::wstring& output, const std::wstring& appDir) {
	const std::string markdown = ReadAllBytes(input);
	if (markdown.empty()) {
		printf("input is empty or unreadable: %s\n", mdpreview::WideToUtf8(input).c_str());
		return 2;
	}
	printf("read %zu bytes\n", markdown.size());

	mdpreview::LocalContent content;
	content.SetAppDir(appDir);

	mdpreview::DocMeta meta;
	meta.path = mdpreview::WideToUtf8(input);
	const size_t slash = input.find_last_of(L"\\/");
	meta.dir = (slash == std::wstring::npos) ? std::string() : mdpreview::WideToUtf8(input.substr(0, slash));
	meta.title = (slash == std::wstring::npos) ? "document.md" : mdpreview::WideToUtf8(input.substr(slash + 1));
	meta.encoding = "UTF-8";

	const mdpreview::RenderResult result = content.UpdateDocument(markdown, meta);
	printf("render ok=%d html=%zu bytes\n", result.ok ? 1 : 0, result.html.size());

	std::string error;
	if (!content.ExportSnapshot(output, error)) {
		printf("export failed: %s\n", error.c_str());
		return 3;
	}
	printf("wrote %s\n", mdpreview::WideToUtf8(output).c_str());
	return 0;
}

int main() {
	::SetConsoleOutputCP(CP_UTF8);
	::setvbuf(stdout, nullptr, _IONBF, 0);

	const std::wstring exeDir = ExeDir();
	// 预览资源已编入 exe；单元测试直接读源码目录里的原始文件。
	const std::wstring appDir = AbsolutePath(exeDir + L"\\..\\..\\..\\preview\\res");

	int argCount = 0;
	LPWSTR* args = ::CommandLineToArgvW(::GetCommandLineW(), &argCount);
	if (args != nullptr && argCount == 4 && _wcsicmp(args[1], L"--render") == 0) {
		const int rc = RenderToFile(args[2], args[3], appDir);
		::LocalFree(args);
		return rc;
	}
	if (args != nullptr) {
		::LocalFree(args);
	}

	printf("Notepad4 Markdown Preview - 自动化测试\n\n");
	const std::wstring docDir = AbsolutePath(exeDir + L"\\..\\..\\..\\tests\\fixtures");

	TestRendering();
	TestSecurity();
	TestAssetRouter(appDir, docDir);
	TestSnapshotRewrite();

	printf("\n共 %d 项检查，失败 %d 项。\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
