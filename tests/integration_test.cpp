// SPDX-License-Identifier: MIT
//
// Notepad4 Markdown Preview - 与真实 Notepad4 的集成测试（内嵌面板）
//
// 流程：
//   1. 启动打了补丁的 Notepad4.exe 并打开一个 Markdown 文件；
//   2. 校验主菜单中存在「Markdown 预览」子菜单；
//   3. 触发 IDM_MDPREVIEW_OPEN，确认预览面板与分隔条作为主窗口的子窗口出现，
//      且编辑器被压缩到左侧（说明是"原窗口内分栏"，而不是新开窗口）；
//   4. 面板内应出现 WebView2 渲染子窗口；
//   5. 向 Scintilla 键入未保存文本，验证不会破坏布局、面板仍然存活；
//   6. 关闭预览，编辑器恢复占满客户区；
//   7. 全程保存窗口截图，便于人工复核。
//
#include <windows.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

namespace {

constexpr UINT IDM_MDPREVIEW_OPEN = 40960;
constexpr UINT IDM_MDPREVIEW_REFRESH = 40961;
constexpr UINT IDM_MDPREVIEW_CLOSE = 40964;
constexpr UINT IDM_MDPREVIEW_AUTOOPEN = 40965;
constexpr UINT IDM_FILE_REVERT = 40002;
constexpr wchar_t kPaneClass[] = L"Notepad4MarkdownPreviewPane";
constexpr wchar_t kSplitterClass[] = L"Notepad4MarkdownSplitter";

int g_failures = 0;

void Check(bool condition, const char* name, const std::string& detail = {}) {
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

std::string Narrow(const std::wstring& text) {
	std::string out;
	if (text.empty()) {
		return out;
	}
	const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
		nullptr, 0, nullptr, nullptr);
	out.resize(static_cast<size_t>(bytes));
	::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
		out.data(), bytes, nullptr, nullptr);
	return out;
}

std::wstring ExeDir() {
	wchar_t buffer[MAX_PATH * 4] = {};
	const DWORD length = ::GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
	std::wstring path(buffer, length);
	const size_t slash = path.find_last_of(L"\\/");
	return (slash == std::wstring::npos) ? path : path.substr(0, slash);
}

bool MenuContainsText(HMENU menu, const wchar_t* needle) {
	if (menu == nullptr) {
		return false;
	}
	const int count = ::GetMenuItemCount(menu);
	for (int i = 0; i < count; ++i) {
		wchar_t text[256] = {};
		if (::GetMenuStringW(menu, i, text, ARRAYSIZE(text), MF_BYPOSITION) > 0) {
			if (wcsstr(text, needle) != nullptr) {
				return true;
			}
		}
		HMENU sub = ::GetSubMenu(menu, i);
		if (sub != nullptr && MenuContainsText(sub, needle)) {
			return true;
		}
	}
	return false;
}


std::wstring DiagnosticsPath() {
	wchar_t buffer[MAX_PATH * 4] = {};
	::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, ARRAYSIZE(buffer));
	wchar_t logDir[MAX_PATH * 4] = {};
	::GetEnvironmentVariableW(L"TEMP", logDir, ARRAYSIZE(logDir));
	std::wstring dir(buffer);
	if (!dir.empty()) {
		dir += L"\\Notepad4MarkdownPreview";
		return dir + L"\\webview2.log";
	}
	return std::wstring(logDir) + L"\\webview2.log";
}

std::string ReadLog() {
	HANDLE file = ::CreateFileW(DiagnosticsPath().c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return {};
	}
	LARGE_INTEGER size = {};
	::GetFileSizeEx(file, &size);
	if (size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) {
		::CloseHandle(file);
		return {};
	}
	std::string content(static_cast<size_t>(size.QuadPart), '\0');
	DWORD read = 0;
	::ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr);
	::CloseHandle(file);
	content.resize(read);
	return content;
}

// 解析 "chars":N，返回页面报告的字符数（0 表示未上报）。
int ParsedChars(const std::string& log) {
	const std::string key = "\"chars\":";
	const size_t at = log.rfind(key);
	if (at == std::string::npos) {
		return -1;
	}
	size_t pos = at + key.size();
	int value = 0;
	while (pos < log.size() && log[pos] >= '0' && log[pos] <= '9') {
		value = value * 10 + (log[pos] - '0');
		++pos;
	}
	return value;
}


// 解析 "images":{"total":N,"loaded":M,...} 中的 loaded 字段。
int ParsedImagesLoaded(const std::string& log) {
	const std::string key = "\"loaded\":";
	const size_t at = log.rfind(key);
	if (at == std::string::npos) {
		return -1;
	}
	size_t pos = at + key.size();
	int value = 0;
	while (pos < log.size() && log[pos] >= '0' && log[pos] <= '9') {
		value = value * 10 + (log[pos] - '0');
		++pos;
	}
	return value;
}

// 临时把 res 目录改名，验证程序在「没有 res 目录」时也能正常工作
// （资源已编译进 exe）。离开作用域时恢复。
struct ResDirHider {
	std::wstring from;
	std::wstring to;
	bool moved = false;

	explicit ResDirHider(const std::wstring& dir) : from(dir), to(dir + L".__hid") {
		if (::GetFileAttributesW(from.c_str()) != INVALID_FILE_ATTRIBUTES) {
			moved = ::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
		}
	}
	~ResDirHider() {
		if (moved) {
			::MoveFileExW(to.c_str(), from.c_str(), MOVEFILE_REPLACE_EXISTING);
		}
	}

	ResDirHider(const ResDirHider&) = delete;
	ResDirHider& operator=(const ResDirHider&) = delete;
};

void KillExistingEditors() {
	HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return;
	}
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	const DWORD self = ::GetCurrentProcessId();
	if (::Process32FirstW(snapshot, &entry)) {
		do {
			if (entry.th32ProcessID == self) {
				continue;
			}
			if (_wcsicmp(entry.szExeFile, L"Notepad4.exe") == 0) {
				HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
				if (process != nullptr) {
					::TerminateProcess(process, 0);
					::CloseHandle(process);
				}
			}
		} while (::Process32NextW(snapshot, &entry));
	}
	::CloseHandle(snapshot);
	::Sleep(300);
}

HWND WaitForMainWindow(DWORD timeoutMs) {
	const DWORD deadline = ::GetTickCount() + timeoutMs;
	while (::GetTickCount() < deadline) {
		HWND hwnd = ::FindWindowW(L"Notepad4", nullptr);
		if (hwnd != nullptr && ::IsWindowVisible(hwnd)) {
			return hwnd;
		}
		::Sleep(100);
	}
	return nullptr;
}

bool WaitUntil(const std::function<bool()>& predicate, DWORD timeoutMs) {
	const DWORD deadline = ::GetTickCount() + timeoutMs;
	while (::GetTickCount() < deadline) {
		if (predicate()) {
			return true;
		}
		::Sleep(100);
	}
	return false;
}

int WindowWidth(HWND hwnd) {
	RECT rc = {};
	return (hwnd != nullptr && ::GetWindowRect(hwnd, &rc)) ? (rc.right - rc.left) : 0;
}

int GetEncoderClsid(const WCHAR* format, CLSID* clsid) {
	UINT num = 0;
	UINT size = 0;
	::Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0) {
		return -1;
	}
	auto* info = static_cast<ImageCodecInfo*>(malloc(size));
	if (info == nullptr) {
		return -1;
	}
	::Gdiplus::GetImageEncoders(num, size, info);
	int found = -1;
	for (UINT i = 0; i < num; ++i) {
		if (wcscmp(info[i].MimeType, format) == 0) {
			*clsid = info[i].Clsid;
			found = static_cast<int>(i);
			break;
		}
	}
	free(info);
	return found;
}

void CaptureWindow(HWND hwnd, const wchar_t* path) {
	if (hwnd == nullptr) {
		return;
	}
	RECT rc = {};
	::GetWindowRect(hwnd, &rc);
	const int width = rc.right - rc.left;
	const int height = rc.bottom - rc.top;
	if (width <= 0 || height <= 0) {
		return;
	}

	HDC screen = ::GetWindowDC(hwnd);
	HDC mem = ::CreateCompatibleDC(screen);
	HBITMAP bmp = ::CreateCompatibleBitmap(screen, width, height);
	HGDIOBJ old = ::SelectObject(mem, bmp);
	// PW_RENDERFULLCONTENT 才能抓到 WebView2 这类子窗口的内容
	::PrintWindow(hwnd, mem, 2);
	::SelectObject(mem, old);

	Bitmap image(bmp, nullptr);
	CLSID pngClsid = {};
	if (GetEncoderClsid(L"image/png", &pngClsid) >= 0) {
		image.Save(path, &pngClsid, nullptr);
	}

	::DeleteObject(bmp);
	::DeleteDC(mem);
	::ReleaseDC(hwnd, screen);
}

std::wstring PaneChildSummary(HWND pane) {
	std::wstring summary;
	HWND child = nullptr;
	for (int i = 0; i < 8; ++i) {
		child = ::FindWindowExW(pane, child, nullptr, nullptr);
		if (child == nullptr) {
			break;
		}
		wchar_t cls[128] = {};
		::GetClassNameW(child, cls, ARRAYSIZE(cls));
		if (!summary.empty()) {
			summary += L", ";
		}
		summary += cls;
	}
	return summary;
}

}  // namespace

int RunTests(int argc, wchar_t** argv) {
	printf("=== Notepad4 内嵌 Markdown 预览集成测试 ===\n");

	KillExistingEditors();

	const std::wstring exeDir = ExeDir();
	// 隐藏 res 目录：若内嵌资源失效，下面的渲染断言会直接失败。
	ResDirHider resHider(exeDir + L"\\res");
	printf("        (res 目录: %s)\n", resHider.moved ? "已隐藏，走内嵌资源" : "不存在");

	const std::wstring editorPath = exeDir + L"\\Notepad4.exe";
	// 可选参数：指定要打开的 Markdown 文件（用于人工核对某个文档的渲染效果）。
	std::wstring fixturePath = exeDir + L"\\..\\..\\..\\tests\\fixtures\\basic.md";
	if (argc > 1 && argv[1] != nullptr && argv[1][0] != L'\0') {
		wchar_t full[MAX_PATH * 4] = {};
		if (::GetFullPathNameW(argv[1], ARRAYSIZE(full), full, nullptr) != 0) {
			fixturePath = full;
		} else {
			fixturePath = argv[1];
		}
	}
	const std::wstring shotPrefix = exeDir + L"\\..\\..\\..\\build\\logs\\shot-";

	if (::GetFileAttributesW(editorPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		printf("  FAIL  未找到 Notepad4.exe：%s\n", Narrow(editorPath).c_str());
		return 1;
	}

	::SetEnvironmentVariableW(L"NOTEPAD4_MDPREVIEW_LOG", L"1");
	::DeleteFileW(DiagnosticsPath().c_str());

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	std::wstring command = L"\"" + editorPath + L"\" \"" + fixturePath + L"\"";
	if (!::CreateProcessW(editorPath.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
			nullptr, exeDir.c_str(), &si, &pi)) {
		printf("  FAIL  CreateProcess failed: %lu\n", ::GetLastError());
		return 1;
	}

	HWND mainWindow = WaitForMainWindow(15000);
	Check(mainWindow != nullptr, "编辑器主窗口出现");
	if (mainWindow == nullptr) {
		::TerminateProcess(pi.hProcess, 0);
		return 1;
	}

	HWND editor = ::FindWindowExW(mainWindow, nullptr, L"Scintilla", nullptr);
	Check(editor != nullptr, "找到 Scintilla 编辑控件");

	RECT client = {};
	::GetClientRect(mainWindow, &client);
	const int clientWidth = client.right - client.left;
	printf("        (client=%d)\n", clientWidth);

	HMENU menu = ::GetMenu(mainWindow);
	Check(menu != nullptr && MenuContainsText(menu, L"Markdown"), "主菜单中包含 Markdown 预览子菜单");

	// --- 「打开 Markdown 时显示预览」默认开启：启动即阅读视图 ---
	HWND pane = nullptr;
	const bool autoOpened = WaitUntil([&]() {
		pane = ::FindWindowExW(mainWindow, nullptr, kPaneClass, nullptr);
		return pane != nullptr && ::IsWindowVisible(pane);
	}, 25000);
	Check(autoOpened, "打开 .md 自动进入预览视图");
	if (!autoOpened) {
		::PostMessageW(mainWindow, WM_CLOSE, 0, 0);
		::WaitForSingleObject(pi.hProcess, 3000);
		::TerminateProcess(pi.hProcess, 0);
		return 1;
	}

	Check(!::IsWindowVisible(editor), "预览视图隐藏编辑器");
	const int readingWidth = WindowWidth(pane);
	Check(readingWidth >= clientWidth - 8, "预览视图下预览占满客户区",
		std::to_string(readingWidth) + " vs " + std::to_string(clientWidth));

	HWND splitter = ::FindWindowExW(mainWindow, nullptr, kSplitterClass, nullptr);

	// 页面渲染完成后会通过 postMessage 回报字符数；这是端到端的"内容真的渲染出来了"证据。
	const bool pageRendered = WaitUntil([]() {
		const std::string log = ReadLog();
		return log.find("page message") != std::string::npos && ParsedChars(log) > 50;
	}, 25000);
	const std::string renderLog = ReadLog();
	Check(pageRendered, "页面完成渲染并回报宿主", "chars=" + std::to_string(ParsedChars(renderLog)));

	// 图片是端到端最容易"看起来渲染了其实没显示"的部分，单独断言。
	const bool imageLoaded = WaitUntil([]() {
		return ParsedImagesLoaded(ReadLog()) >= 1;
	}, 15000);
	Check(imageLoaded, "文档中的本地图片已加载并显示",
		"loaded=" + std::to_string(ParsedImagesLoaded(ReadLog())));

	// 可选：截图前多等一会儿，便于人工核对远程图片（默认不等，保持测试速度）。
	//   set NOTEPAD4_MDPREVIEW_SHOT_DELAY=6000
	{
		wchar_t delayText[16] = {};
		if (::GetEnvironmentVariableW(L"NOTEPAD4_MDPREVIEW_SHOT_DELAY", delayText, ARRAYSIZE(delayText)) > 0) {
			const int delay = _wtoi(delayText);
			if (delay > 0 && delay <= 60000) {
				::Sleep(static_cast<DWORD>(delay));
			}
		}
	}

	CaptureWindow(mainWindow, (shotPrefix + L"inline-reading.png").c_str());

	// --- 关闭预览：回到普通编辑视图 ---
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_CLOSE, 0), 0);
	const bool backToEdit = WaitUntil([&]() {
		return ::IsWindowVisible(editor) && !::IsWindowVisible(pane);
	}, 10000);
	Check(backToEdit, "关闭预览后编辑器重新出现");

	const int editorFullWidth = WindowWidth(editor);
	Check(editorFullWidth >= clientWidth - 8, "编辑器恢复占满客户区",
		std::to_string(editorFullWidth) + " vs " + std::to_string(clientWidth));

	// --- 取消勾选后，重新加载 .md 应停留在编辑视图 ---
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_AUTOOPEN, 0), 0);
	::Sleep(300);
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_FILE_REVERT, 0), 0);
	::Sleep(2000);
	Check(!::IsWindowVisible(pane), "取消勾选后打开 .md 为编辑视图");

	// 恢复默认开关，避免影响后续用例与 ini
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_AUTOOPEN, 0), 0);
	::Sleep(300);

	// --- 手动打开：分栏预览（编辑器 + 预览） ---
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_OPEN, 0), 0);
	const bool paneAppeared = WaitUntil([&]() {
		return pane != nullptr && ::IsWindowVisible(pane) && ::IsWindowVisible(splitter);
	}, 15000);
	Check(paneAppeared, "手动打开后预览面板与分隔条同时出现");

	const int paneWidth = WindowWidth(pane);
	const int editorSplitWidth = WindowWidth(editor);
	printf("        (pane=%d, splitter=%d, editor=%d)\n",
		paneWidth, WindowWidth(splitter), editorSplitWidth);

	Check(paneWidth > 120, "预览面板有实际宽度", std::to_string(paneWidth));
	Check(editorSplitWidth > 0 && editorSplitWidth < editorFullWidth,
		"编辑器被压缩到左侧（原窗口内分栏）",
		std::to_string(editorSplitWidth) + " < " + std::to_string(editorFullWidth));

	// 面板尺寸 + 分隔条 + 编辑器应当大致等于客户区宽度
	const int total = paneWidth + WindowWidth(splitter) + editorSplitWidth;
	Check(total >= clientWidth - 8 && total <= clientWidth + 8,
		"三部分宽度之和等于客户区宽度",
		std::to_string(total) + " vs " + std::to_string(clientWidth));

	// WebView2 在容器内创建自己的子窗口
	const bool webviewChild = WaitUntil([&]() {
		const std::wstring summary = PaneChildSummary(pane);
		return !summary.empty();
	}, 15000);
	const std::wstring childSummary = PaneChildSummary(pane);
	Check(webviewChild, "面板内出现 WebView2 渲染子窗口", Narrow(childSummary));

	::Sleep(1200);
	CaptureWindow(mainWindow, (shotPrefix + L"inline-open.png").c_str());

	// --- 未保存内容：键入后不应破坏布局 ---
	if (editor != nullptr) {
		::SetForegroundWindow(mainWindow);
		::SendMessageW(editor, WM_CHAR, '\r', 1);
		const char* text = "## live update";
		for (const char* p = text; *p != '\0'; ++p) {
			::SendMessageW(editor, WM_CHAR, static_cast<WPARAM>(*p), 1);
		}
		::Sleep(1200);
		Check(::IsWindowVisible(pane) && WindowWidth(pane) == paneWidth,
			"输入未保存内容后布局保持不变",
			std::to_string(WindowWidth(pane)));
		CaptureWindow(mainWindow, (shotPrefix + L"inline-edited.png").c_str());
	}

	// --- 拖动分隔条 ---
	{
		const int before = WindowWidth(pane);
		::SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3, 200));
		::SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-120, 200));
		::SendMessageW(splitter, WM_LBUTTONUP, 0, MAKELPARAM(-120, 200));
		::Sleep(400);
		const int after = WindowWidth(pane);
		// 跨进程模拟鼠标时坐标换算与真实鼠标存在差异，这里只要求宽度确实改变。
		Check(after != before && after > 200, "拖动分隔条改变预览面板宽度",
			std::to_string(before) + " -> " + std::to_string(after));

		// 双击恢复默认宽度（客户区的一半）
		::SendMessageW(splitter, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(3, 200));
		::Sleep(400);
		const int reset = WindowWidth(pane);
		Check(reset + 12 >= clientWidth / 2 && reset <= clientWidth / 2 + 12,
			"双击分隔条恢复默认宽度",
			std::to_string(reset) + " vs " + std::to_string(clientWidth / 2));
	}

	// --- 刷新命令 ---
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_REFRESH, 0), 0);
	::Sleep(800);
	Check(::IsWindowVisible(pane), "手动刷新后预览面板仍然可见");

	// --- 关闭 ---
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_CLOSE, 0), 0);
	const bool closed = WaitUntil([&]() {
		return !::IsWindowVisible(pane) && !::IsWindowVisible(splitter);
	}, 8000);
	Check(closed, "关闭后预览面板与分隔条隐藏");

	const int editorRestored = WindowWidth(editor);
	Check(editorRestored >= clientWidth - 8, "关闭后编辑器恢复占满客户区",
		std::to_string(editorRestored) + " vs " + std::to_string(clientWidth));
	CaptureWindow(mainWindow, (shotPrefix + L"inline-closed.png").c_str());

	// --- 再次打开 ---
	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_OPEN, 0), 0);
	const bool reopened = WaitUntil([&]() {
		return ::IsWindowVisible(pane) && WindowWidth(pane) > 120;
	}, 15000);
	Check(reopened, "预览可重复打开");

	::PostMessageW(mainWindow, WM_COMMAND, MAKEWPARAM(IDM_MDPREVIEW_CLOSE, 0), 0);
	::Sleep(500);

	// 编辑器进程应当一直存活
	Check(::WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT, "全程编辑器进程未退出");

	// 退出：文档有未保存修改，直接结束进程避免保存对话框
	::TerminateProcess(pi.hProcess, 0);
	::WaitForSingleObject(pi.hProcess, 3000);
	::CloseHandle(pi.hProcess);
	::CloseHandle(pi.hThread);

	printf("\n失败 %d 项。\n", g_failures);
	return (g_failures == 0) ? 0 : 1;
}

int main() {
	::setvbuf(stdout, nullptr, _IONBF, 0);

	GdiplusStartupInput input;
	ULONG_PTR token = 0;
	GdiplusStartup(&token, &input, nullptr);

	int argc = 0;
	LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	const int result = RunTests(argc, argv);
	if (argv != nullptr) {
		::LocalFree(argv);
	}

	GdiplusShutdown(token);
	return result;
}
