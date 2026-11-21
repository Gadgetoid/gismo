#pragma once

#include <windows.h>

#include <stdint.h>

namespace gismo {

constexpr int camera_width = 320;
constexpr int camera_height = 240;
constexpr int camera_max_zoom = 4;

class camera_driver;

class camera {
public:
    camera();
    ~camera();

    bool available() const;
    bool start(HWND notify_window, UINT notify_message);
    void stop();
    bool running() const;
    bool copy_frame(uint16_t *target);
    bool set_zoom(int level);
    int zoom() const;

private:
    camera_driver *driver;
};

}
