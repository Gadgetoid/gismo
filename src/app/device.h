#pragma once

#include <windows.h>

namespace gismo {

struct device_profile {
    const wchar_t *name;
    const wchar_t *gps_port;
    DWORD gps_baud;
    const wchar_t *backlight;
    bool capture_all_keys;
    bool rocker_shares_pad;
    int key_previous_page;
    int key_next_page;
    int key_camera;
    int key_home;
    int key_mail;
    int key_windows;
    int key_ok;
};

extern const device_profile device;

}
