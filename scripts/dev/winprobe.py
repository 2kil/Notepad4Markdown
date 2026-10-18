"""Find a window by class/title, optionally send a command, and capture it.

Usage:
  python scripts/winprobe.py list
  python scripts/winprobe.py capture <class> <out.png> [renderfull]
"""
import ctypes
import struct
import sys
import time
import zlib
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


def write_png(path, width, height, rgb):
    raw = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + \
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        handle.write(chunk(b"IEND", b""))


def list_windows():
    rows = []

    def callback(hwnd, _):
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
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        rows.append("0x%08X vis=%d pid=%-6d %-34s %-30s (%d,%d)-(%d,%d)" % (
            hwnd, user32.IsWindowVisible(hwnd), pid.value, cls.value, title[:40],
            rect.left, rect.top, rect.right, rect.bottom))
        return True

    user32.EnumWindows(EnumWindowsProc(callback), 0)
    for row in rows:
        print(row)


def capture(cls_name, out, render_full):
    hwnd = user32.FindWindowW(cls_name, None)
    if not hwnd:
        print("window not found:", cls_name)
        return 1
    rect = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    if width <= 0 or height <= 0:
        print("bad size", width, height)
        return 1

    dc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(dc)
    bmp = gdi32.CreateCompatibleBitmap(dc, width, height)
    gdi32.SelectObject(mem, bmp)
    flag = 2 if render_full else 0
    ok = user32.PrintWindow(hwnd, mem, flag)
    print("PrintWindow ok=", ok, "size=", width, height)

    info = BITMAPINFO()
    info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.bmiHeader.biWidth = width
    info.bmiHeader.biHeight = -height
    info.bmiHeader.biPlanes = 1
    info.bmiHeader.biBitCount = 32
    info.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(mem, bmp, 0, height, buf, ctypes.byref(info), 0)
    data = buf.raw
    rgb = bytearray()
    for i in range(0, len(data), 4):
        rgb += data[i + 2:i + 3] + data[i + 1:i + 2] + data[i:i + 1]
    write_png(out, width, height, bytes(rgb))

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, dc)
    print("saved", out)
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if sys.argv[1] == "list":
        list_windows()
        return 0
    if sys.argv[1] == "capture":
        return capture(sys.argv[2], sys.argv[3], len(sys.argv) > 4)
    return 1


if __name__ == "__main__":
    sys.exit(main())
