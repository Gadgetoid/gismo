#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>

#define WINAPI
#define CALLBACK
#define MAX_PATH 260

typedef int BOOL;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef unsigned int UINT;
typedef uintptr_t UINT_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef DWORD COLORREF;
typedef void *LPVOID;
typedef void *HANDLE;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;

struct shim_object;
typedef shim_object *HGDIOBJ;
typedef shim_object *HDC;
typedef shim_object *HBITMAP;
typedef shim_object *HFONT;
typedef shim_object *HBRUSH;
typedef struct shim_window *HWND;
typedef struct shim_instance *HINSTANCE;

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(colour) ((BYTE)(colour))
#define GetGValue(colour) ((BYTE)((colour) >> 8))
#define GetBValue(colour) ((BYTE)((colour) >> 16))
#define LOWORD(value) ((WORD)((uintptr_t)(value) & 0xFFFF))
#define HIWORD(value) ((WORD)(((uintptr_t)(value) >> 16) & 0xFFFF))

struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};

struct POINT {
    LONG x;
    LONG y;
};

struct SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
};

union ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    uint64_t QuadPart;
};

struct WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    wchar_t cFileName[MAX_PATH];
};

struct LOGFONTW {
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    wchar_t lfFaceName[32];
};

struct BITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
};

struct BITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    DWORD bmiColors[1];
};

struct PAINTSTRUCT {
    HDC hdc;
    BOOL fErase;
    RECT rcPaint;
};

struct MSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
};

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

struct WNDCLASSW {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HANDLE hIcon;
    HANDLE hCursor;
    HBRUSH hbrBackground;
    LPCWSTR lpszMenuName;
    LPCWSTR lpszClassName;
};

struct CRITICAL_SECTION {
    void *opaque;
};

#define FW_NORMAL 400
#define FW_BOLD 700
#define CLEARTYPE_QUALITY 5
#define BI_RGB 0
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0
#define TRANSPARENT 1
#define OPAQUE 2
#define SRCCOPY 0x00CC0020

#define DT_TOP 0x0
#define DT_LEFT 0x0
#define DT_CENTER 0x1
#define DT_RIGHT 0x2
#define DT_VCENTER 0x4
#define DT_BOTTOM 0x8
#define DT_WORDBREAK 0x10
#define DT_SINGLELINE 0x20
#define DT_NOCLIP 0x100
#define DT_CALCRECT 0x400
#define DT_NOPREFIX 0x800
#define DT_END_ELLIPSIS 0x8000

#define MB_OK 0
#define MB_ICONHAND 0x10
#define FILE_ATTRIBUTE_TEMPORARY 0x100
#define FILE_SHARE_READ 0x1
#define FILE_BEGIN 0
#define CP_UTF8 65001
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_PAINT 0x000F
#define WM_ERASEBKGND 0x0014
#define WM_SETTINGCHANGE 0x001A
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_APP 0x8000

#define VK_BACK 0x08
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_F6 0x75

DWORD GetTickCount();
DWORD GetLastError();
void Sleep(DWORD milliseconds);
BOOL MessageBeep(UINT type);
int GetSystemMetrics(int index);
void GetSystemTime(SYSTEMTIME *time);
void GetLocalTime(SYSTEMTIME *time);
BOOL SetSystemTime(const SYSTEMTIME *time);
BOOL SystemTimeToFileTime(const SYSTEMTIME *time, FILETIME *file_time);
void SystemIdleTimerReset();

HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *info, UINT usage, void **bits, HANDLE section, DWORD offset);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL DeleteObject(HGDIOBJ object);
HBRUSH CreateSolidBrush(COLORREF colour);
int FillRect(HDC dc, const RECT *area, HBRUSH brush);
COLORREF SetTextColor(HDC dc, COLORREF colour);
int SetBkMode(HDC dc, int mode);
HFONT CreateFontIndirectW(const LOGFONTW *font);
int DrawTextW(HDC dc, const wchar_t *text, int length, RECT *area, UINT format);
BOOL BitBlt(HDC target, int x, int y, int width, int height, HDC source, int source_x, int source_y, DWORD operation);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
HDC BeginPaint(HWND window, PAINTSTRUCT *paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT *paint);
BOOL GetClientRect(HWND window, RECT *area);
BOOL InvalidateRect(HWND window, const RECT *area, BOOL erase);
BOOL UpdateWindow(HWND window);
HWND SetCapture(HWND window);
BOOL ReleaseCapture();
BOOL PtInRect(const RECT *area, POINT point);
UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, void *function);
BOOL KillTimer(HWND window, UINT_PTR id);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL SetForegroundWindow(HWND window);
HWND FindWindowW(LPCWSTR class_name, LPCWSTR title);
BOOL DestroyWindow(HWND window);
void PostQuitMessage(int code);
LRESULT DefWindowProcW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

DWORD GetModuleFileNameW(HINSTANCE module, wchar_t *name, DWORD size);
BOOL CreateDirectoryW(LPCWSTR path, void *security);
DWORD GetFileAttributesW(LPCWSTR path);
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *found);
BOOL FindNextFileW(HANDLE search, WIN32_FIND_DATAW *found);
BOOL FindClose(HANDLE search);
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, HANDLE templ);
BOOL ReadFile(HANDLE file, void *data, DWORD length, DWORD *read, void *overlapped);
DWORD SetFilePointer(HANDLE file, LONG distance, LONG *distance_high, DWORD method);
BOOL WriteFile(HANDLE file, const void *data, DWORD length, DWORD *written, void *overlapped);
BOOL CloseHandle(HANDLE handle);

int MultiByteToWideChar(UINT code_page, DWORD flags, const char *text, int length, wchar_t *output, int output_size);
int WideCharToMultiByte(UINT code_page, DWORD flags, const wchar_t *text, int length, char *output, int output_size, const char *fallback, BOOL *used_fallback);
int wsprintfW(wchar_t *output, const wchar_t *format, ...);
wchar_t *lstrcpyW(wchar_t *target, const wchar_t *source);
wchar_t *lstrcpynW(wchar_t *target, const wchar_t *source, int size);
wchar_t *lstrcatW(wchar_t *target, const wchar_t *source);
int lstrlenW(const wchar_t *text);
int lstrcmpW(const wchar_t *first, const wchar_t *second);
int lstrcmpiW(const wchar_t *first, const wchar_t *second);
FILE *_wfopen(const wchar_t *path, const wchar_t *mode);
