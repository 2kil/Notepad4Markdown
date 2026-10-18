"""List visible top-level windows (title / class / rect) for debugging.

Usage: python scripts/winlist.py [filter-substring]
"""
import ctypes
import sys
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def main():
    needle = sys.argv[1].lower() if len(sys.argv) > 1 else ""
    results = []

    def callback(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        title = ""
        if length:
            buf = ctypes.create_unicode_buffer(length + 1)
            user32.GetWindowTextW(hwnd, buf, length + 1)
            title = buf.value
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls, 256)
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        line = "0x%08X  %-40s  %-28s  (%d,%d)-(%d,%d)" % (
            hwnd, cls.value, title[:60], rect.left, rect.top, rect.right, rect.bottom)
        if needle and needle not in title.lower() and needle not in cls.value.lower():
            return True
        results.append(line)
        return True

    user32.EnumWindows(EnumWindowsProc(callback), 0)
    for line in results:
        print(line)
    print("total:", len(results))


if __name__ == "__main__":
    main()
