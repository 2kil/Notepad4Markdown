// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - 文本编码转换实现
//
#include "text_encoding.h"

namespace mdpreview {

std::string WideToUtf8(std::wstring_view text) {
	if (text.empty()) {
		return {};
	}
	const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		nullptr, 0, nullptr, nullptr);
	if (bytes <= 0) {
		return {};
	}
	std::string out(static_cast<size_t>(bytes), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		out.data(), bytes, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(std::string_view text) {
	if (text.empty()) {
		return {};
	}
	const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		nullptr, 0);
	if (chars <= 0) {
		return {};
	}
	std::wstring out(static_cast<size_t>(chars), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), chars);
	return out;
}

std::string ToUtf8(std::string_view bytes, UINT codePage) {
	if (bytes.empty()) {
		return {};
	}
	if (codePage == CP_UTF8 || codePage == 65001 || codePage == 0) {
		return std::string(bytes);
	}
	const int chars = ::MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
		nullptr, 0);
	if (chars <= 0) {
		// 无法解码时按原样透传，交给渲染层处理，避免直接丢内容。
		return std::string(bytes);
	}
	std::wstring wide(static_cast<size_t>(chars), L'\0');
	::MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), chars);
	return WideToUtf8(wide);
}

}  // namespace mdpreview
