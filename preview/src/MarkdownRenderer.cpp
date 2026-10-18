// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - Markdown 渲染层实现
//
#include "MarkdownRenderer.h"

#include "md4c.h"
#include "md4c-html.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace mdpreview {

namespace {

// 1x1 透明 GIF，用于替换被安全策略拦截的图片，避免产生真实请求。
constexpr char kPlaceholderImage[] =
	"data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";

constexpr size_t kMaxDataUriBytes = 2u * 1024u * 1024u;

void AppendOutput(const MD_CHAR* text, MD_SIZE size, void* userdata) {
	static_cast<std::string*>(userdata)->append(text, size);
}

bool IsAsciiSpace(char ch) noexcept {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

char ToLowerAscii(char ch) noexcept {
	return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) noexcept {
	if (text.size() < prefix.size()) {
		return false;
	}
	for (size_t i = 0; i < prefix.size(); ++i) {
		if (ToLowerAscii(text[i]) != ToLowerAscii(prefix[i])) {
			return false;
		}
	}
	return true;
}

// 按 HTML 属性上下文转义。
void AppendHtmlEscaped(std::string_view value, std::string& out) {
	for (const char ch : value) {
		switch (ch) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		case '\'': out += "&#39;"; break;
		default: out.push_back(ch); break;
		}
	}
}

// 还原 md4c 输出的实体转义，只处理它自身可能产生的 5 种实体。
// 这样既不会引入"二次解码"漏洞，也能让 URL 判定看到真实字符。
std::string UnescapeBasicEntities(std::string_view value) {
	std::string out;
	out.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] != '&') {
			out.push_back(value[i]);
			continue;
		}
		if (value.compare(i, 5, "&amp;") == 0) {
			out.push_back('&');
			i += 4;
		} else if (value.compare(i, 4, "&lt;") == 0) {
			out.push_back('<');
			i += 3;
		} else if (value.compare(i, 4, "&gt;") == 0) {
			out.push_back('>');
			i += 3;
		} else if (value.compare(i, 6, "&quot;") == 0) {
			out.push_back('"');
			i += 5;
		} else if (value.compare(i, 5, "&#39;") == 0) {
			out.push_back('\'');
			i += 4;
		} else {
			out.push_back('&');
		}
	}
	return out;
}

struct UrlParts {
	std::string cleaned;   // 去除控制字符、裁剪首尾空白后的 URL
	std::string scheme;    // 小写 scheme；相对 URL 为空
	bool hasScheme = false;
	bool protocolRelative = false;
};

UrlParts AnalyzeUrl(std::string_view raw) {
	UrlParts parts;
	parts.cleaned.reserve(raw.size());
	for (const char ch : raw) {
		const unsigned char uch = static_cast<unsigned char>(ch);
		if (uch < 0x20 || uch == 0x7f) {
			continue;   // 浏览器会忽略控制字符，这里也一并剔除
		}
		parts.cleaned.push_back(ch);
	}
	// 裁剪首尾空白
	size_t begin = 0;
	size_t end = parts.cleaned.size();
	while (begin < end && IsAsciiSpace(parts.cleaned[begin])) {
		++begin;
	}
	while (end > begin && IsAsciiSpace(parts.cleaned[end - 1])) {
		--end;
	}
	if (begin > 0 || end < parts.cleaned.size()) {
		parts.cleaned = parts.cleaned.substr(begin, end - begin);
	}

	if (parts.cleaned.size() >= 2 && parts.cleaned[0] == '/' && parts.cleaned[1] == '/') {
		parts.protocolRelative = true;
		return parts;
	}

	// scheme 判定：只看前 32 个字符，遇到 '/', '?', '#' 即停止
	size_t colon = std::string::npos;
	const size_t limit = std::min<size_t>(parts.cleaned.size(), 32);
	for (size_t i = 0; i < limit; ++i) {
		const char ch = parts.cleaned[i];
		if (ch == ':') {
			colon = i;
			break;
		}
		if (ch == '/' || ch == '?' || ch == '#') {
			break;
		}
	}
	if (colon != std::string::npos && colon > 0) {
		std::string scheme;
		bool valid = true;
		for (size_t i = 0; i < colon; ++i) {
			const char ch = parts.cleaned[i];
			const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
			const bool digit = (ch >= '0' && ch <= '9');
			if (i == 0 && !alpha) {
				valid = false;
				break;
			}
			if (!alpha && !digit && ch != '+' && ch != '-' && ch != '.') {
				valid = false;
				break;
			}
			scheme.push_back(ToLowerAscii(ch));
		}
		if (valid) {
			parts.hasScheme = true;
			parts.scheme = std::move(scheme);
		}
	}
	return parts;
}

}  // namespace

bool IsUrlAllowed(std::string_view url, UrlUse use, std::string& reason) {
	const UrlParts parts = AnalyzeUrl(url);
	reason.clear();

	if (parts.cleaned.empty()) {
		reason = "空 URL";
		return false;
	}
	if (parts.protocolRelative) {
		reason = "禁止协议相对 URL (//host)";
		return false;
	}
	if (!parts.hasScheme) {
		return true;   // 相对路径 / 锚点，由资源解析层再做范围校验
	}

	const std::string& scheme = parts.scheme;
	if (scheme == "http" || scheme == "https") {
		return true;
	}
	if (use == UrlUse::Link) {
		if (scheme == "mailto") {
			return true;
		}
		reason = "链接协议不在白名单内: " + scheme;
		return false;
	}

	// 图片
	if (scheme == "data") {
		if (!StartsWithIgnoreCase(parts.cleaned, "data:image/")) {
			reason = "data: URL 仅允许图片类型";
			return false;
		}
		if (parts.cleaned.size() > kMaxDataUriBytes) {
			reason = "data: URL 体积超过限制";
			return false;
		}
		return true;
	}
	if (scheme == "file") {
		// 文档里写死 file:///C:/... 等于让预览读任意本地文件，一律拒绝。
		// 快照导出时内部生成的 file:/// 链接不经过这里（由服务器侧路由生成）。
		reason = "文档内不允许 file: 绝对路径";
		return false;
	}
	reason = "图片协议不在白名单内: " + scheme;
	return false;
}

namespace {

// 解析标签属性，重建 <img>/<a> 标签。
// md4c 生成的所有属性值都用双引号包裹，且 '"' 在值内被转义为 &quot;，
// 因此按 '"' 切分是安全的。
std::string RebuildTag(std::string_view tag, bool isImage, RenderStats& stats, bool& blockedAny) {
	std::string out;
	out.push_back('<');
	size_t i = 1;

	// 标签名
	while (i < tag.size() && !IsAsciiSpace(tag[i]) && tag[i] != '>' && tag[i] != '/') {
		out.push_back(tag[i]);
		++i;
	}

	bool sawLazy = false;
	bool sawRel = false;

	while (i < tag.size()) {
		if (tag[i] == '>') {
			if (isImage && !sawLazy) {
				// 不用 loading="lazy"：WebView2 面板可能被其他窗口遮挡，
				// 此时 Chromium 会无限期推迟懒加载，用户切回来才逐张出现，
				// 看起来像"图片显示不出来"。预览是本地文档，直接加载成本可忽略。
				out += " decoding=\"async\"";
			}
			out.push_back('>');
			++i;
			return out;
		}
		if (tag[i] == '/') {
			out.push_back(tag[i]);
			++i;
			continue;
		}
		if (IsAsciiSpace(tag[i])) {
			out.push_back(' ');
			++i;
			continue;
		}

		const size_t nameStart = i;
		while (i < tag.size() && tag[i] != '=' && tag[i] != '>' && tag[i] != '/' && !IsAsciiSpace(tag[i])) {
			++i;
		}
		const std::string_view name = tag.substr(nameStart, i - nameStart);

		if (i < tag.size() && tag[i] == '=' && i + 1 < tag.size() && tag[i + 1] == '"') {
			const size_t valueStart = i + 2;
			size_t valueEnd = valueStart;
			while (valueEnd < tag.size() && tag[valueEnd] != '"') {
				++valueEnd;
			}
			const std::string_view value = tag.substr(valueStart, valueEnd - valueStart);

			const bool isHref = (name == "href");
			const bool isSrc = (name == "src");
			if (isHref || isSrc) {
				const std::string decoded = UnescapeBasicEntities(value);
				std::string reason;
				const UrlUse use = isSrc ? UrlUse::Image : UrlUse::Link;
				const bool allowed = IsUrlAllowed(decoded, use, reason);
				if (isImage) {
					++stats.images;
				} else if (isHref) {
					++stats.links;
				}
				std::string finalValue;
				if (allowed) {
					finalValue = decoded;
				} else {
					++stats.blockedUrls;
					blockedAny = true;
					finalValue = isSrc ? kPlaceholderImage : "#md-blocked-link";
				}
				out += name;
				out += "=\"";
				AppendHtmlEscaped(finalValue, out);
				out.push_back('"');
				if (!allowed) {
					out += " data-blocked=\"1\"";
				}
			} else {
				out += name;
				out += "=\"";
				out.append(value.data(), value.size());
				out.push_back('"');
				if (name == "rel") {
					sawRel = true;
				}
			}
			i = (valueEnd < tag.size()) ? valueEnd + 1 : tag.size();
			continue;
		}

		if (i < tag.size() && tag[i] == '=') {
			out.push_back('=');
			++i;
			continue;
		}
		out.append(name.data(), name.size());
	}

	if (isImage && !sawLazy) {
		out += " decoding=\"async\"";
	}
	(void)sawRel;
	return out;
}


// ---------------------------------------------------------------------------
// 原始 HTML 清洗
//
// 大量 GitHub 风格 README 用 html 做排版（<div align>、<img>、<details>…），
// 完全禁用会让文档变成一堆源码。这里改为"白名单重建"：只保留允许的标签与
// 属性，其余一律丢弃，从而在不引入脚本执行风险的前提下正常排版。
// ---------------------------------------------------------------------------

// 这些标签连同其内容一起丢弃，避免脚本/样式的正文作为纯文本漏出。
bool DropsContent(std::string_view name) {
	static constexpr const char* kNames[] = {
		"script", "style", "iframe", "object", "embed", "noscript", "template",
		"svg", "math", "form", "textarea", "select", "button",
	};
	for (const char* item : kNames) {
		if (name == item) {
			return true;
		}
	}
	return false;
}

bool IsVoidTag(std::string_view name) {
	static constexpr const char* kVoid[] = {
		"br", "hr", "img", "input", "source", "col", "wbr",
	};
	for (const char* item : kVoid) {
		if (name == item) {
			return true;
		}
	}
	return false;
}

bool IsAllowedTag(std::string_view name) {
	static constexpr const char* kTags[] = {
		"a", "abbr", "b", "bdi", "bdo", "blockquote", "br", "caption", "center", "cite",
		"code", "col", "colgroup", "dd", "del", "details", "dfn", "div", "dl", "dt",
		"em", "figcaption", "figure", "h1", "h2", "h3", "h4", "h5", "h6", "hr", "i",
		"img", "input", "ins", "kbd", "li", "mark", "ol", "p", "picture", "pre", "q",
		"rp", "rt", "ruby", "s", "samp", "small", "source", "span", "strike", "strong",
		"sub", "summary", "sup", "table", "tbody", "td", "tfoot", "th", "thead", "time",
		"tr", "tt", "u", "ul", "var", "wbr",
	};
	for (const char* item : kTags) {
		if (name == item) {
			return true;
		}
	}
	return false;
}

bool IsAllowedAttr(std::string_view tag, std::string_view attr) {
	// 通用属性
	static constexpr const char* kCommon[] = {
		"align", "class", "dir", "height", "lang", "style", "title", "valign", "width",
	};
	for (const char* item : kCommon) {
		if (attr == item) {
			return true;
		}
	}

	if (tag == "a") {
		return attr == "href" || attr == "name" || attr == "rel";
	}
	if (tag == "img") {
		return attr == "src" || attr == "alt" || attr == "srcset" || attr == "sizes" || attr == "loading";
	}
	if (tag == "source") {
		return attr == "srcset" || attr == "media" || attr == "type" || attr == "sizes";
	}
	if (tag == "input") {
		return attr == "type" || attr == "checked" || attr == "disabled";
	}
	if (attr == "colspan" || attr == "rowspan" || attr == "scope") {
		return tag == "td" || tag == "th";
	}
	if (attr == "start" || attr == "reversed" || attr == "type") {
		return tag == "ol" || tag == "li";
	}
	if (attr == "open") {
		return tag == "details";
	}
	if (attr == "datetime" || attr == "cite") {
		return tag == "del" || tag == "ins" || tag == "time";
	}
	if (attr == "span") {
		return tag == "col" || tag == "colgroup";
	}
	if (attr == "border" || attr == "cellpadding" || attr == "cellspacing" || attr == "bgcolor") {
		return tag == "table" || tag == "td" || tag == "th";
	}
	return false;
}

bool IsBoolAttr(std::string_view attr) {
	return attr == "checked" || attr == "disabled" || attr == "open" || attr == "reversed";
}

// 允许的 URL 协议；相对路径与锚点也放行（它们由本地资源路由与页面处理）。
bool IsSafeUrlValue(std::string_view value, bool allowData) {
	size_t i = 0;
	while (i < value.size() && (value[i] == ' ' || value[i] == '\t' || value[i] == '\n' || value[i] == '\r')) {
		++i;
	}
	std::string head;
	head.reserve(16);
	for (; i < value.size() && head.size() < 12; ++i) {
		const char ch = value[i];
		if (ch == ':') {
			// 比较协议名
			for (char& c : head) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			if (head == "http" || head == "https" || head == "mailto") {
				return true;
			}
			if (allowData && head == "data") {
				// 只允许 data:image/*
				const std::string_view rest = value.substr(i + 1);
				return rest.rfind("image/", 0) == 0;
			}
			return false;
		}
		if (ch == '/' || ch == '#' || ch == '?' || ch == '.') {
			// 相对路径 / 锚点 / 查询串
			return true;
		}
		if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '+' || ch == '-' || ch == '.')) {
			return true;  // 其它奇怪开头，交给浏览器按相对路径处理
		}
		head.push_back(ch);
	}
	return true;  // 没有冒号，视为相对路径
}

bool IsSafeStyleValue(std::string_view value) {
	auto lower = [](std::string_view text) {
		std::string out(text);
		for (char& c : out) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	};
	const std::string text = lower(value);
	return text.find("expression") == std::string::npos
		&& text.find("javascript:") == std::string::npos
		&& text.find("behavior") == std::string::npos
		&& text.find("@import") == std::string::npos;
}

void AppendAttrValue(std::string& out, std::string_view value) {
	out.push_back('"');
	for (const char ch : value) {
		if (ch == '"') {
			out += "&quot;";
		} else if (ch == '<') {
			out += "&lt;";
		} else {
			out.push_back(ch);
		}
	}
	out.push_back('"');
}

// 重建单个开始标签；不合法时返回空串。
std::string RebuildSanitizedTag(std::string_view tag, int& blockedUrls) {
	std::string out = "<";
	std::string name;

	// tag 形如 "<h1 ...>", 名字从下标 1 开始
	size_t i = 1;
	while (i < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[i])) || tag[i] == '-')) {
		name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i]))));
		++i;
	}
	if (name.empty() || !IsAllowedTag(name)) {
		return {};
	}
	out += name;

	while (i < tag.size()) {
		while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) {
			++i;
		}
		if (i >= tag.size() || tag[i] == '>' || tag[i] == '/') {
			break;
		}

		// 属性名
		std::string attr;
		while (i < tag.size()) {
			const char ch = tag[i];
			if (std::isspace(static_cast<unsigned char>(ch)) || ch == '=' || ch == '>' || ch == '/') {
				break;
			}
			attr.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
			++i;
		}
		if (attr.empty()) {
			++i;
			continue;
		}

		// 可选的值
		std::string value;
		bool hasValue = false;
		while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) {
			++i;
		}
		if (i < tag.size() && tag[i] == '=') {
			++i;
			hasValue = true;
			while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) {
				++i;
			}
			if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
				const char quote = tag[i++];
				while (i < tag.size() && tag[i] != quote) {
					value.push_back(tag[i++]);
				}
				if (i < tag.size()) {
					++i;
				}
			} else {
				while (i < tag.size() && !std::isspace(static_cast<unsigned char>(tag[i]))
					&& tag[i] != '>' && tag[i] != '/') {
					value.push_back(tag[i++]);
				}
			}
		}

		if (!IsAllowedAttr(name, attr)) {
			continue;
		}
		if (attr == "type" && name == "input" && value != "checkbox") {
			// 只允许任务列表复选框
			return {};
		}
		if (attr == "style" && !IsSafeStyleValue(value)) {
			continue;
		}
		if ((attr == "href" || attr == "src" || attr == "srcset") && !IsSafeUrlValue(value, attr == "src")) {
			++blockedUrls;
			continue;
		}

		out.push_back(' ');
		out += attr;
		if (hasValue && !value.empty()) {
			out.push_back('=');
			AppendAttrValue(out, value);
		} else if (IsBoolAttr(attr)) {
			// 布尔属性保持无值形式
		}
	}
	out.push_back('>');
	return out;
}

std::string SanitizeHtml(std::string_view html, RenderStats& stats) {
	std::string out;
	out.reserve(html.size() + 32);

	size_t i = 0;
	while (i < html.size()) {
		const char ch = html[i];
		if (ch != '<') {
			out.push_back(ch);
			++i;
			continue;
		}

		// 注释整段丢弃
		if (html.compare(i, 4, "<!--") == 0) {
			const size_t end = html.find("-->", i + 4);
			i = (end == std::string_view::npos) ? html.size() : end + 3;
			continue;
		}
		// <!DOCTYPE ...> / <? ... ?> 丢弃
		if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
			const size_t end = html.find('>', i);
			i = (end == std::string_view::npos) ? html.size() : end + 1;
			continue;
		}

		const size_t gt = html.find('>', i);
		if (gt == std::string_view::npos) {
			out += "&lt;";
			++i;
			continue;
		}
		const std::string_view tag = html.substr(i, gt - i + 1);

		// 结束标签
		if (tag.size() >= 3 && tag[1] == '/') {
			std::string name;
			size_t k = 2;
			while (k < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[k])) || tag[k] == '-')) {
				name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(tag[k]))));
				++k;
			}
			if (!name.empty() && IsAllowedTag(name) && !IsVoidTag(name)) {
				out += "</";
				out += name;
				out.push_back('>');
			}
			i = gt + 1;
			continue;
		}

		// 开始标签
		if (tag.size() >= 2 && (std::isalpha(static_cast<unsigned char>(tag[1])))) {
			std::string lowerName;
			for (size_t k = 1; k < tag.size(); ++k) {
				const char c = tag[k];
				if (!std::isalnum(static_cast<unsigned char>(c))) {
					break;
				}
				lowerName.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			}
			if (DropsContent(lowerName)) {
				// 跳过整个元素（含内容）
				const std::string closing = "</" + lowerName;
				const size_t closeAt = html.find(closing, gt + 1);
				if (closeAt == std::string_view::npos) {
					i = html.size();
				} else {
					const size_t closeEnd = html.find('>', closeAt);
					i = (closeEnd == std::string_view::npos) ? html.size() : closeEnd + 1;
				}
				continue;
			}

			int blocked = 0;
			const std::string rebuilt = RebuildSanitizedTag(tag, blocked);
			stats.blockedUrls += static_cast<size_t>(blocked);
			if (!rebuilt.empty()) {
				out += rebuilt;
			}
			i = gt + 1;
			continue;
		}

		out += "&lt;";
		++i;
	}

	return out;
}

std::string PostProcessHtml(std::string_view html, RenderStats& stats) {
	std::string out;
	out.reserve(html.size() + html.size() / 8 + 64);

	size_t i = 0;
	bool blockedAny = false;

	while (i < html.size()) {
		const size_t lt = html.find('<', i);
		if (lt == std::string_view::npos) {
			out.append(html.data() + i, html.size() - i);
			break;
		}
		out.append(html.data() + i, lt - i);

		const size_t gt = html.find('>', lt);
		if (gt == std::string_view::npos) {
			out.append(html.data() + lt, html.size() - lt);
			break;
		}

		const std::string_view tag = html.substr(lt, gt - lt + 1);
		if (tag.size() >= 4 && StartsWithIgnoreCase(tag, "<img")) {
			out += RebuildTag(tag, true, stats, blockedAny);
		} else if (tag.size() >= 3 && StartsWithIgnoreCase(tag, "<a ") ) {
			out += RebuildTag(tag, false, stats, blockedAny);
		} else if (tag.size() >= 7 && StartsWithIgnoreCase(tag, "<table")) {
			out += "<div class=\"md-table\">";
			out.append(tag.data(), tag.size());
			++stats.tables;
		} else if (tag.size() >= 8 && StartsWithIgnoreCase(tag, "</table>")) {
			out.append(tag.data(), tag.size());
			out += "</div>";
		} else {
			out.append(tag.data(), tag.size());
		}
		i = gt + 1;
	}

	return out;
}

}  // namespace

RenderResult RenderMarkdown(std::string_view markdownUtf8, const RenderOptions& options) {
	(void)options;
	RenderResult result;
	const auto started = std::chrono::steady_clock::now();

	if (markdownUtf8.size() > 0x7fffffffu) {
		result.error = "文档体积超限";
		return result;
	}

	std::string raw;
	raw.reserve(markdownUtf8.size() + markdownUtf8.size() / 4 + 64);

	// 保留原始 HTML（大量 README 依赖 <div align>、<img>、<details> 排版），
	// 但随后由 SanitizeHtml 做标签/属性白名单重建，脚本与事件属性不会被保留。
	const unsigned parserFlags = static_cast<unsigned>(MD_DIALECT_GITHUB);
	const unsigned rendererFlags = 0;

	const int rc = md_html(markdownUtf8.data(), static_cast<MD_SIZE>(markdownUtf8.size()),
		AppendOutput, &raw, parserFlags, rendererFlags);
	if (rc != 0) {
		result.error = "Markdown 解析失败";
		return result;
	}

	const std::string sanitized = SanitizeHtml(raw, result.stats);
	result.html = PostProcessHtml(sanitized, result.stats);
	result.ok = true;
	const auto finished = std::chrono::steady_clock::now();
	result.stats.elapsedMs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());
	return result;
}

// ---------------------------------------------------------------------------
// 快照导出：把相对 URL 改写为 file:/// 绝对 URL
// ---------------------------------------------------------------------------

namespace {

std::string PercentEncodePathForFileUrl(std::string_view path) {
	std::string out;
	out.reserve(path.size() + 16);
	for (const char ch : path) {
		const unsigned char uch = static_cast<unsigned char>(ch);
		if (ch == '\\') {
			out.push_back('/');
		} else if (ch == ' ') {
			out += "%20";
		} else if (ch == '#') {
			out += "%23";
		} else if (ch == '?') {
			out += "%3F";
		} else if (ch == '%') {
			out += "%25";
		} else if (uch >= 0x80) {
			char buffer[8];
			snprintf(buffer, sizeof(buffer), "%%%02X", uch);
			out += buffer;
		} else {
			out.push_back(ch);
		}
	}
	return out;
}

bool IsPathSeparator(char ch) noexcept {
	return ch == '\\' || ch == '/';
}

// 去掉 URL 的查询串与锚点，得到纯路径部分。
std::string_view StripQueryAndFragment(std::string_view url) {
	const size_t cut = url.find_first_of("?#");
	return (cut == std::string_view::npos) ? url : url.substr(0, cut);
}

// 规范化 Windows 路径中的 "." 与 ".." 段（不访问文件系统）。
std::string NormalizeSegments(std::string_view path) {
	std::vector<std::string> stack;
	size_t i = 0;
	while (i <= path.size()) {
		size_t j = i;
		while (j < path.size() && !IsPathSeparator(path[j])) {
			++j;
		}
		const std::string_view segment = path.substr(i, j - i);
		if (segment.empty() || segment == ".") {
			// 忽略
		} else if (segment == "..") {
			if (!stack.empty()) {
				stack.pop_back();
			}
		} else {
			stack.emplace_back(segment);
		}
		if (j >= path.size()) {
			break;
		}
		i = j + 1;
	}

	std::string normalized;
	for (size_t k = 0; k < stack.size(); ++k) {
		if (k != 0) {
			normalized.push_back('\\');
		}
		normalized += stack[k];
	}
	return normalized;
}

bool StartsWithIgnoreCasePath(std::string_view text, std::string_view prefix) {
	if (text.size() < prefix.size()) {
		return false;
	}
	for (size_t i = 0; i < prefix.size(); ++i) {
		const char a = (text[i] == '/') ? '\\' : ToLowerAscii(text[i]);
		const char b = (prefix[i] == '/') ? '\\' : ToLowerAscii(prefix[i]);
		if (a != b) {
			return false;
		}
	}
	return true;
}

// 把相对路径与 baseDir 拼接；若结果跳出 baseDir 则返回空串。
std::string ResolveWithinBaseDir(std::string_view baseDir, std::string_view relative) {
	std::string combined(baseDir);
	if (!combined.empty() && !IsPathSeparator(combined.back())) {
		combined.push_back('\\');
	}
	combined.append(relative.data(), relative.size());

	const std::string baseNormalized = NormalizeSegments(baseDir);
	const std::string resolved = NormalizeSegments(combined);
	if (baseNormalized.empty() || resolved.empty()) {
		return {};
	}
	// 结果必须位于 baseDir 之内，否则视为越界
	if (!StartsWithIgnoreCasePath(resolved, baseNormalized)) {
		return {};
	}
	if (resolved.size() <= baseNormalized.size()) {
		return {};
	}
	const char boundary = resolved[baseNormalized.size()];
	if (boundary != '\\' && boundary != '/') {
		return {};
	}
	return resolved;
}

}  // namespace

std::string RewriteRelativeUrlsToFile(std::string_view html, std::string_view baseDir) {
	std::string out;
	out.reserve(html.size() + 64);

	size_t i = 0;
	while (i < html.size()) {
		const size_t lt = html.find('<', i);
		if (lt == std::string_view::npos) {
			out.append(html.data() + i, html.size() - i);
			break;
		}
		out.append(html.data() + i, lt - i);
		const size_t gt = html.find('>', lt);
		if (gt == std::string_view::npos) {
			out.append(html.data() + lt, html.size() - lt);
			break;
		}
		const std::string_view tag = html.substr(lt, gt - lt + 1);
		const bool isImage = StartsWithIgnoreCase(tag, "<img");
		if (!isImage) {
			out.append(tag.data(), tag.size());
			i = gt + 1;
			continue;
		}

		// 仅处理 src 属性
		std::string rebuilt;
		rebuilt.push_back('<');
		size_t p = 1;
		while (p < tag.size() && !IsAsciiSpace(tag[p]) && tag[p] != '>' && tag[p] != '/') {
			rebuilt.push_back(tag[p]);
			++p;
		}
		bool handled = false;
		while (p < tag.size()) {
			if (tag[p] == '>' || tag[p] == '/' || IsAsciiSpace(tag[p])) {
				rebuilt.push_back(tag[p]);
				++p;
				continue;
			}
			const size_t nameStart = p;
			while (p < tag.size() && tag[p] != '=' && tag[p] != '>' && tag[p] != '/' && !IsAsciiSpace(tag[p])) {
				++p;
			}
			const std::string_view name = tag.substr(nameStart, p - nameStart);
			if (p < tag.size() && tag[p] == '=' && p + 1 < tag.size() && tag[p + 1] == '"') {
				const size_t valueStart = p + 2;
				size_t valueEnd = valueStart;
				while (valueEnd < tag.size() && tag[valueEnd] != '"') {
					++valueEnd;
				}
				const std::string_view value = tag.substr(valueStart, valueEnd - valueStart);
				if (name == "src" && !handled) {
					handled = true;
					const std::string decoded = UnescapeBasicEntities(value);
					std::string target;
					std::string reason;
					if (IsUrlAllowed(decoded, UrlUse::Image, reason)) {
						const UrlParts parts = AnalyzeUrl(decoded);
						if (!parts.hasScheme) {
							const std::string_view pathOnly = StripQueryAndFragment(parts.cleaned);
							const std::string resolved = ResolveWithinBaseDir(baseDir, pathOnly);
							if (!resolved.empty()) {
								target = "file:///";
								target += PercentEncodePathForFileUrl(resolved);
							}
						} else if (parts.scheme == "file") {
							target = parts.cleaned;
						} else {
							target = parts.cleaned;
						}
					}
					if (target.empty()) {
						target = kPlaceholderImage;
					}
					rebuilt += "src=\"";
					AppendHtmlEscaped(target, rebuilt);
					rebuilt.push_back('"');
				} else {
					rebuilt += name;
					rebuilt += "=\"";
					rebuilt.append(value.data(), value.size());
					rebuilt.push_back('"');
				}
				p = (valueEnd < tag.size()) ? valueEnd + 1 : tag.size();
				continue;
			}
			rebuilt.append(name.data(), name.size());
		}
		out += rebuilt;
		i = gt + 1;
	}
	return out;
}

}  // namespace mdpreview
