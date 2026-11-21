#include "log.h"

#include <windows.h>

#include <stdarg.h>

namespace gismo {

namespace {

HANDLE log_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION log_lock;

}

void log_open(const wchar_t *path) {
    InitializeCriticalSection(&log_lock);
    log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void log_line(const wchar_t *format, ...) {
    if (log_file == INVALID_HANDLE_VALUE) {
        return;
    }
    wchar_t message[256];
    int prefix = wsprintfW(message, L"%8u ", GetTickCount());
    va_list arguments;
    va_start(arguments, format);
    wvsprintfW(message + prefix, format, arguments);
    va_end(arguments);
    lstrcatW(message, L"\r\n");

    char encoded[512];
    int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, encoded, sizeof(encoded), nullptr, nullptr);
    if (length <= 1) {
        return;
    }
    EnterCriticalSection(&log_lock);
    DWORD written;
    WriteFile(log_file, encoded, length - 1, &written, nullptr);
    FlushFileBuffers(log_file);
    LeaveCriticalSection(&log_lock);
    OutputDebugStringW(message);
}

void log_close() {
    if (log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }
}

}
