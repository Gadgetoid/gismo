#pragma once

#include <windows.h>

#include "nmea.h"

namespace gismo {

class gps_port {
public:
    gps_port();
    ~gps_port();

    bool start(const wchar_t *port_name, DWORD baud_rate, HWND notify_window, UINT notify_message);
    bool start_replay(const wchar_t *path, HWND notify_window, UINT notify_message);
    void stop();

    gps_state snapshot();
    bool connected() const {
        return port_open;
    }

    bool replaying() const {
        return replay_path[0] != 0;
    }

private:
    static DWORD WINAPI thread_entry(LPVOID parameter);
    void run();
    void run_replay();
    bool open_port();
    void feed(const char *data, int length);

    wchar_t port[16];
    wchar_t replay_path[MAX_PATH];
    DWORD baud;
    HWND window;
    UINT message;
    HANDLE thread;
    HANDLE port_handle;
    volatile bool stopping;
    volatile bool port_open;
    CRITICAL_SECTION lock;
    nmea_parser parser;
    uint32_t last_position_updates;
    uint32_t last_satellite_updates;
};

}
