#include "gps_port.h"

#include "log.h"

#include <stdlib.h>
#include <string.h>

namespace gismo {

gps_port::gps_port() : baud(4800), window(nullptr), message(0), thread(nullptr), port_handle(INVALID_HANDLE_VALUE), stopping(false), port_open(false), last_position_updates(0), last_satellite_updates(0) {
    port[0] = 0;
    replay_path[0] = 0;
    InitializeCriticalSection(&lock);
}

gps_port::~gps_port() {
    stop();
    DeleteCriticalSection(&lock);
}

bool gps_port::start(const wchar_t *port_name, DWORD baud_rate, HWND notify_window, UINT notify_message) {
    stop();
    lstrcpynW(port, port_name, sizeof(port) / sizeof(port[0]));
    replay_path[0] = 0;
    baud = baud_rate;
    window = notify_window;
    message = notify_message;
    stopping = false;
    thread = CreateThread(nullptr, 0, thread_entry, this, 0, nullptr);
    return thread != nullptr;
}

bool gps_port::start_replay(const wchar_t *path, HWND notify_window, UINT notify_message) {
    stop();
    lstrcpynW(replay_path, path, MAX_PATH);
    window = notify_window;
    message = notify_message;
    stopping = false;
    thread = CreateThread(nullptr, 0, thread_entry, this, 0, nullptr);
    return thread != nullptr;
}

void gps_port::stop() {
    if (thread == nullptr) {
        return;
    }
    stopping = true;
    if (port_handle != INVALID_HANDLE_VALUE) {
        SetCommMask(port_handle, 0);
    }
    WaitForSingleObject(thread, 3000);
    CloseHandle(thread);
    thread = nullptr;
}

gps_state gps_port::snapshot() {
    EnterCriticalSection(&lock);
    gps_state copy = parser.state();
    LeaveCriticalSection(&lock);
    return copy;
}

DWORD WINAPI gps_port::thread_entry(LPVOID parameter) {
    gps_port *self = static_cast<gps_port *>(parameter);
    if (self->replaying()) {
        self->run_replay();
    } else {
        self->run();
    }
    return 0;
}

void gps_port::feed(const char *data, int length) {
    EnterCriticalSection(&lock);
    parser.feed(data, length);
    const gps_state &state = parser.state();
    bool changed = state.position_updates != last_position_updates || state.satellite_updates != last_satellite_updates;
    last_position_updates = state.position_updates;
    last_satellite_updates = state.satellite_updates;
    LeaveCriticalSection(&lock);
    if (changed) {
        PostMessageW(window, message, 0, 0);
    }
}

void gps_port::run_replay() {
    HANDLE file = CreateFileW(replay_path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log_line(L"gps: can't open %s, error %u", replay_path, GetLastError());
        return;
    }
    DWORD size = GetFileSize(file, nullptr);
    char *data = static_cast<char *>(malloc(size));
    DWORD read = 0;
    bool loaded = data && ReadFile(file, data, size, &read, nullptr) && read == size;
    CloseHandle(file);
    if (!loaded) {
        log_line(L"gps: can't read %s", replay_path);
        free(data);
        return;
    }
    log_line(L"gps: replaying %s, %u bytes", replay_path, size);
    port_open = true;
    PostMessageW(window, message, 0, 0);

    while (!stopping) {
        DWORD position = 0;
        while (position < size && !stopping) {
            DWORD end = position;
            while (end < size && data[end] != '\n') {
                end++;
            }
            if (end < size) {
                end++;
            }
            bool epoch_end = end - position > 6 && memcmp(data + position + 3, "RMC", 3) == 0;
            feed(data + position, int(end - position));
            position = end;
            if (epoch_end) {
                Sleep(1000);
            }
        }
    }
    free(data);
    port_open = false;
}

bool gps_port::open_port() {
    port_handle = CreateFileW(port, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (port_handle == INVALID_HANDLE_VALUE) {
        log_line(L"gps: %s open failed, error %u", port, GetLastError());
        return false;
    }
    log_line(L"gps: %s open", port);
    DCB settings = {};
    settings.DCBlength = sizeof(settings);
    if (GetCommState(port_handle, &settings)) {
        settings.BaudRate = baud;
        settings.ByteSize = 8;
        settings.Parity = NOPARITY;
        settings.StopBits = ONESTOPBIT;
        SetCommState(port_handle, &settings);
    }
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    SetCommTimeouts(port_handle, &timeouts);
    return true;
}

void gps_port::run() {
    char buffer[256];
    while (!stopping) {
        if (port_handle == INVALID_HANDLE_VALUE) {
            if (!open_port()) {
                Sleep(2000);
                continue;
            }
            port_open = true;
            PostMessageW(window, message, 0, 0);
        }

        DWORD read = 0;
        if (!ReadFile(port_handle, buffer, sizeof(buffer), &read, nullptr)) {
            CloseHandle(port_handle);
            port_handle = INVALID_HANDLE_VALUE;
            port_open = false;
            PostMessageW(window, message, 0, 0);
            Sleep(1000);
            continue;
        }
        if (read == 0) {
            continue;
        }

        feed(buffer, int(read));
    }
    if (port_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(port_handle);
        port_handle = INVALID_HANDLE_VALUE;
    }
    port_open = false;
}

}
