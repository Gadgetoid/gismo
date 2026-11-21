#include <windows.h>
#include <aygshell.h>
#include <cs.h>
#include <csmedia.h>
#include <msgqueue.h>

#include <stdlib.h>
#include <string.h>

#include "log.h"

using namespace gismo;

namespace {

constexpr int preview_pin = 2;
constexpr int still_pin = 1;
constexpr int preview_buffers = 2;
constexpr int max_ranges = 8;
constexpr int canvas_width = 320;
constexpr int canvas_height = 240;
constexpr int key_camera = 0xC3;
constexpr int key_ok = 0x75;
constexpr DWORD still_timeout_ms = 6000;
constexpr DWORD still_display_ms = 3000;
constexpr DWORD preview_stall_ms = 2000;
constexpr DWORD tick_ms = 250;
constexpr int max_frame_logs = 6;
constexpr int shutter_radius = 26;

const wchar_t *const zoom_names[] = {L"1x", L"2x", L"3x", L"4x"};
const int still_widths[] = {800, 1280, 1600, 2048};
constexpr int still_width_count = sizeof(still_widths) / sizeof(still_widths[0]);

struct pin_stream {
    HANDLE pin;
    HANDLE queue;
    CS_DATARANGE_VIDEO ranges[max_ranges];
    int range_count;
    CS_STREAM_DESCRIPTOR descriptors[preview_buffers];
    int descriptor_count;
    bool client_memory;
};

struct surface {
    HDC dc;
    HBITMAP bitmap;
    uint16_t *bits;
    int width;
    int height;
    int stride;
};

struct state {
    HWND window;
    surface screen;
    surface canvas;
    HFONT small_font;
    HFONT large_font;

    HANDLE camera;
    pin_stream preview;
    CS_DATARANGE_VIDEO preview_range;
    pin_stream still;
    int still_range;
    int zoom;

    DWORD frames;
    DWORD frames_at_second;
    DWORD second_tick;
    DWORD fps;
    DWORD last_frame_tick;
    LONGLONG drops;
    int frame_logs;
    bool showing_still;
    DWORD still_shown_tick;
    DWORD still_finished_tick;
    bool waiting_for_preview;
    bool preview_running;
    bool still_armed;
    int image_rotation;
    const uint8_t *still_frame;
    int still_width;
    int still_height;
    wchar_t status[160];
    wchar_t folder[MAX_PATH];
};

state app;

bool property(HANDLE device, const GUID &set, ULONG id, ULONG flags, void *output, DWORD output_size, DWORD *returned) {
    CSPROPERTY request = {};
    request.Set = set;
    request.Id = id;
    request.Flags = flags;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(device, IOCTL_CS_PROPERTY, &request, sizeof(request), output, output_size, &bytes, nullptr);
    if (returned) {
        *returned = bytes;
    }
    return ok != FALSE;
}

bool pin_property(ULONG pin, ULONG id, void *output, DWORD output_size, DWORD *returned) {
    CSP_PIN request = {};
    request.Property.Set = CSPROPSETID_Pin;
    request.Property.Id = id;
    request.Property.Flags = CSPROPERTY_TYPE_GET;
    request.PinId = pin;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(app.camera, IOCTL_CS_PROPERTY, &request, sizeof(request), output, output_size, &bytes, nullptr);
    if (returned) {
        *returned = bytes;
    }
    return ok != FALSE;
}

void describe_range(const CS_DATARANGE_VIDEO &range, wchar_t *text) {
    const BITMAPINFOHEADER &header = range.VideoInfoHeader.bmiHeader;
    bool rgb = memcmp(&range.DataRange.SubFormat, &MEDIASUBTYPE_RGB565, sizeof(GUID)) == 0;
    bool yv12 = memcmp(&range.DataRange.SubFormat, &MEDIASUBTYPE_YV12, sizeof(GUID)) == 0;
    LONGLONG interval = range.VideoInfoHeader.AvgTimePerFrame;
    wsprintfW(text, L"%s %dx%d %d bpp compression %08x planes %d sample %u, %d fps", rgb ? L"RGB565" : yv12 ? L"YV12" : L"other", header.biWidth, header.biHeight, header.biBitCount, unsigned(header.biCompression), header.biPlanes, unsigned(range.DataRange.SampleSize), interval ? int(10000000 / interval) : 0);
}

bool load_ranges(ULONG pin, pin_stream &stream) {
    CSMULTIPLE_ITEM probe = {};
    DWORD needed = 0;
    pin_property(pin, CSPROPERTY_PIN_DATARANGES, &probe, sizeof(probe), &needed);
    if (needed < sizeof(CSMULTIPLE_ITEM)) {
        log_line(L"pin %u ranges: need %u bytes, error %u", unsigned(pin), unsigned(needed), GetLastError());
        return false;
    }
    uint8_t *buffer = static_cast<uint8_t *>(malloc(needed));
    DWORD returned = 0;
    if (!pin_property(pin, CSPROPERTY_PIN_DATARANGES, buffer, needed, &returned)) {
        log_line(L"pin %u ranges failed, error %u", unsigned(pin), GetLastError());
        free(buffer);
        return false;
    }
    const CSMULTIPLE_ITEM *items = reinterpret_cast<const CSMULTIPLE_ITEM *>(buffer);
    int count = int((items->Size - sizeof(CSMULTIPLE_ITEM)) / sizeof(CS_DATARANGE_VIDEO));
    stream.range_count = 0;
    for (int index = 0; index < count && index < max_ranges; index++) {
        memcpy(&stream.ranges[index], buffer + sizeof(CSMULTIPLE_ITEM) + index * sizeof(CS_DATARANGE_VIDEO), sizeof(CS_DATARANGE_VIDEO));
        wchar_t text[160];
        describe_range(stream.ranges[index], text);
        log_line(L"pin %u range %d: %s", unsigned(pin), index, text);
        stream.range_count++;
    }
    free(buffer);
    return stream.range_count > 0;
}

bool connection_state(pin_stream &stream, CSSTATE value) {
    CSSTATE requested = value;
    if (!property(stream.pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_STATE, CSPROPERTY_TYPE_SET, &requested, sizeof(requested), nullptr)) {
        log_line(L"state %d failed, error %u", int(value), GetLastError());
        return false;
    }
    return true;
}

bool buffer_command(pin_stream &stream, DWORD command, CS_STREAM_DESCRIPTOR &descriptor) {
    DWORD bytes = 0;
    if (!DeviceIoControl(stream.pin, IOCTL_CS_BUFFERS, &command, sizeof(command), &descriptor, sizeof(descriptor), &bytes, nullptr)) {
        log_line(L"buffer command %u failed, error %u", unsigned(command), GetLastError());
        return false;
    }
    return true;
}

bool arm_buffers(ULONG pin_id, pin_stream &stream, DWORD size, int buffers) {
    if (!connection_state(stream, CSSTATE_PAUSE)) {
        return false;
    }
    for (int index = 0; index < buffers; index++) {
        CS_STREAM_DESCRIPTOR &descriptor = stream.descriptors[index];
        void *data = descriptor.CsStreamHeader.Data;
        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.CsStreamHeader.Size = sizeof(CSSTREAM_HEADER);
        descriptor.CsStreamHeader.FrameExtent = size;
        descriptor.CsFrameInfo.ExtendedHeaderSize = sizeof(CS_FRAME_INFO);
        descriptor.CsStreamHeader.Data = data;
        if (stream.client_memory && descriptor.CsStreamHeader.Data == nullptr) {
            descriptor.CsStreamHeader.Data = malloc(size);
            if (descriptor.CsStreamHeader.Data == nullptr) {
                log_line(L"pin %u: can't allocate %u bytes", unsigned(pin_id), unsigned(size));
                return false;
            }
        }
        if (!buffer_command(stream, CS_ALLOCATE, descriptor)) {
            return false;
        }
        if (index >= stream.descriptor_count) {
            stream.descriptor_count = index + 1;
        }
        if (!buffer_command(stream, CS_ENQUEUE, descriptor)) {
            return false;
        }
    }
    return true;
}

void disarm_buffers(pin_stream &stream) {
    connection_state(stream, CSSTATE_STOP);
    CS_MSGQUEUE_BUFFER message;
    DWORD read;
    DWORD flags;
    while (stream.queue && ReadMsgQueue(stream.queue, &message, sizeof(message), &read, 0, &flags)) {
    }
    for (int index = 0; index < stream.descriptor_count; index++) {
        buffer_command(stream, CS_DEALLOCATE, stream.descriptors[index]);
    }
}

bool open_stream(ULONG pin_id, pin_stream &stream, const CS_DATARANGE_VIDEO &range, int buffers) {
    MSGQUEUEOPTIONS options = {};
    options.dwSize = sizeof(options);
    options.dwMaxMessages = 8;
    options.cbMaxMessage = sizeof(CS_MSGQUEUE_BUFFER);
    options.bReadAccess = TRUE;
    stream.queue = CreateMsgQueue(nullptr, &options);
    stream.pin = CreateFileW(L"PIN1:", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (stream.queue == nullptr || stream.pin == INVALID_HANDLE_VALUE) {
        log_line(L"pin %u: queue or PIN1: open failed, error %u", unsigned(pin_id), GetLastError());
        return false;
    }

    CSPROPERTY_STREAMEX_S instantiate = {};
    instantiate.CsPin.Property.Set = CSPROPSETID_StreamEx;
    instantiate.CsPin.Property.Id = CSPROPERTY_STREAMEX_INIT;
    instantiate.CsPin.Property.Flags = CSPROPERTY_TYPE_SET;
    instantiate.CsPin.PinId = pin_id;
    instantiate.hMsgQueue = stream.queue;
    DWORD bytes = 0;
    if (!DeviceIoControl(stream.pin, IOCTL_STREAM_INSTANTIATE, &instantiate, sizeof(instantiate), nullptr, 0, &bytes, nullptr)) {
        log_line(L"pin %u: instantiate failed, error %u", unsigned(pin_id), GetLastError());
        return false;
    }

    static CS_DATAFORMAT_VIDEOINFOHEADER format;
    memset(&format, 0, sizeof(format));
    memcpy(&format.DataFormat, &range.DataRange, sizeof(CSDATAFORMAT));
    format.DataFormat.FormatSize = sizeof(format);
    memcpy(&format.VideoInfoHeader, &range.VideoInfoHeader, sizeof(CS_VIDEOINFOHEADER));
    if (!property(stream.pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_DATAFORMAT, CSPROPERTY_TYPE_SET, &format, sizeof(format), nullptr)) {
        log_line(L"pin %u: data format failed, error %u", unsigned(pin_id), GetLastError());
        return false;
    }

    CSALLOCATOR_FRAMING framing = {};
    DWORD returned = 0;
    property(stream.pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_ALLOCATORFRAMING, CSPROPERTY_TYPE_GET, &framing, sizeof(framing), &returned);
    stream.client_memory = framing.RequirementsFlags != CSPROPERTY_BUFFER_DRIVER;
    DWORD size = range.DataRange.SampleSize;
    stream.descriptor_count = 0;
    for (int index = 0; index < buffers; index++) {
        stream.descriptors[index].CsStreamHeader.Data = nullptr;
    }
    if (!arm_buffers(pin_id, stream, size, buffers)) {
        return false;
    }
    log_line(L"pin %u open: %u x %u bytes, memory model %u", unsigned(pin_id), unsigned(buffers), unsigned(size), unsigned(framing.RequirementsFlags));
    return true;
}

void close_stream(pin_stream &stream) {
    if (stream.pin != INVALID_HANDLE_VALUE && stream.pin != nullptr) {
        connection_state(stream, CSSTATE_STOP);
        CS_MSGQUEUE_BUFFER message;
        DWORD read;
        DWORD flags;
        while (stream.queue && ReadMsgQueue(stream.queue, &message, sizeof(message), &read, 0, &flags)) {
        }
        for (int index = 0; index < stream.descriptor_count; index++) {
            buffer_command(stream, CS_DEALLOCATE, stream.descriptors[index]);
            if (stream.client_memory) {
                free(stream.descriptors[index].CsStreamHeader.Data);
                stream.descriptors[index].CsStreamHeader.Data = nullptr;
            }
        }
        stream.descriptor_count = 0;
        CloseHandle(stream.pin);
        stream.pin = INVALID_HANDLE_VALUE;
    }
    if (stream.queue) {
        CloseMsgQueue(stream.queue);
        stream.queue = nullptr;
    }
}

CS_STREAM_DESCRIPTOR *find_descriptor(pin_stream &stream, PCS_STREAM_DESCRIPTOR reported) {
    uintptr_t value = reinterpret_cast<uintptr_t>(reported) & 0x01FFFFFF;
    for (int index = 0; index < stream.descriptor_count; index++) {
        if ((reinterpret_cast<uintptr_t>(&stream.descriptors[index]) & 0x01FFFFFF) == value) {
            return &stream.descriptors[index];
        }
    }
    return nullptr;
}

void source_point(int x, int y, int width, int height, int orientation, int &source_x, int &source_y) {
    if (orientation >= 360) {
        y = canvas_height - 1 - y;
    }
    switch (orientation % 360) {
    case 90:
        source_x = y * width / canvas_height;
        source_y = height - 1 - x * height / canvas_width;
        break;
    case 180:
        source_x = width - 1 - x * width / canvas_width;
        source_y = height - 1 - y * height / canvas_height;
        break;
    case 270:
        source_x = width - 1 - y * width / canvas_height;
        source_y = x * height / canvas_width;
        break;
    default:
        source_x = x * width / canvas_width;
        source_y = y * height / canvas_height;
        break;
    }
}

void show_rgb565_frame(const uint16_t *frame, int width, int height) {
    for (int y = 0; y < canvas_height; y++) {
        uint16_t *target = app.canvas.bits + y * app.canvas.stride;
        for (int x = 0; x < canvas_width; x++) {
            int source_x;
            int source_y;
            source_point(x, y, width, height, app.image_rotation, source_x, source_y);
            target[x] = frame[source_y * width + source_x];
        }
    }
}

uint16_t yuv_to_565(int luma, int blue_difference, int red_difference) {
    int c = luma - 16;
    int d = blue_difference - 128;
    int e = red_difference - 128;
    int red = (298 * c + 409 * e + 128) >> 8;
    int green = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int blue = (298 * c + 516 * d + 128) >> 8;
    red = red < 0 ? 0 : red > 255 ? 255 : red;
    green = green < 0 ? 0 : green > 255 ? 255 : green;
    blue = blue < 0 ? 0 : blue > 255 ? 255 : blue;
    return uint16_t((red >> 3) << 11 | (green >> 2) << 5 | blue >> 3);
}

void show_yv12_frame(const uint8_t *frame, int width, int height) {
    const uint8_t *luma = frame;
    const uint8_t *red_plane = frame + width * height;
    const uint8_t *blue_plane = red_plane + (width / 2) * (height / 2);
    for (int y = 0; y < canvas_height; y++) {
        uint16_t *target = app.canvas.bits + y * app.canvas.stride;
        for (int x = 0; x < canvas_width; x++) {
            int source_x;
            int source_y;
            source_point(x, y, width, height, app.image_rotation, source_x, source_y);
            int chroma = (source_y / 2) * (width / 2) + source_x / 2;
            target[x] = yuv_to_565(luma[source_y * width + source_x], blue_plane[chroma], red_plane[chroma]);
        }
    }
}

void redraw() {
    InvalidateRect(app.window, nullptr, FALSE);
}

void set_status(const wchar_t *text) {
    lstrcpynW(app.status, text, 160);
    log_line(L"%s", text);
    redraw();
}

void on_preview_frames() {
    CS_MSGQUEUE_BUFFER message;
    DWORD read = 0;
    DWORD flags = 0;
    while (ReadMsgQueue(app.preview.queue, &message, sizeof(message), &read, 0, &flags)) {
        CS_STREAM_DESCRIPTOR *descriptor = find_descriptor(app.preview, message.pStreamDescriptor);
        if (app.frame_logs < max_frame_logs) {
            log_line(L"preview frame: descriptor %08x %s used %u picture %u drops %u options %x", unsigned(reinterpret_cast<uintptr_t>(message.pStreamDescriptor)), descriptor ? L"known" : L"unknown", descriptor ? unsigned(descriptor->CsStreamHeader.DataUsed) : 0, descriptor ? unsigned(descriptor->CsFrameInfo.PictureNumber) : 0, descriptor ? unsigned(descriptor->CsFrameInfo.DropCount) : 0, descriptor ? unsigned(descriptor->CsStreamHeader.OptionsFlags) : 0);
            app.frame_logs++;
        }
        if (descriptor == nullptr) {
            continue;
        }
        DWORD now = GetTickCount();
        if (app.waiting_for_preview) {
            log_line(L"preview resumed %u ms after the still", now - app.still_finished_tick);
            app.waiting_for_preview = false;
        }
        app.last_frame_tick = now;
        app.frames++;
        app.drops = descriptor->CsFrameInfo.DropCount;
        if (!app.showing_still) {
            const BITMAPINFOHEADER &header = app.preview_range.VideoInfoHeader.bmiHeader;
            show_rgb565_frame(static_cast<const uint16_t *>(descriptor->CsStreamHeader.Data), header.biWidth, header.biHeight < 0 ? -header.biHeight : header.biHeight);
        }
        buffer_command(app.preview, CS_ENQUEUE, *descriptor);
        redraw();
    }
}

bool start_preview() {
    DWORD started = GetTickCount();
    bool opened = open_stream(preview_pin, app.preview, app.preview_range, preview_buffers);
    app.preview_running = opened && connection_state(app.preview, CSSTATE_RUN);
    app.last_frame_tick = GetTickCount();
    log_line(L"preview started in %u ms: %s", GetTickCount() - started, app.preview_running ? L"ok" : L"failed");
    return app.preview_running;
}

void stop_preview() {
    DWORD started = GetTickCount();
    close_stream(app.preview);
    app.preview_running = false;
    log_line(L"preview stopped in %u ms", GetTickCount() - started);
}

void on_tick() {
    DWORD now = GetTickCount();
    if (now - app.second_tick >= 1000) {
        app.fps = (app.frames - app.frames_at_second) * 1000 / (now - app.second_tick);
        app.frames_at_second = app.frames;
        app.second_tick = now;
    }
    if (app.showing_still && now - app.still_shown_tick > still_display_ms) {
        app.showing_still = false;
    }
    if (app.preview_running && now - app.last_frame_tick > preview_stall_ms) {
        log_line(L"no preview frames for %u ms; reopening the preview", now - app.last_frame_tick);
        app.frame_logs = 0;
        stop_preview();
        start_preview();
    }
    redraw();
}

int still_range_index(int width) {
    for (int index = 0; index < app.still.range_count; index++) {
        if (app.still.ranges[index].VideoInfoHeader.bmiHeader.biWidth == width) {
            return index;
        }
    }
    return -1;
}

void take_still() {
    if (app.still.pin == INVALID_HANDLE_VALUE) {
        set_status(L"still pin not open");
        return;
    }
    CSPROPERTY_VIDEOCONTROL_MODE_S mode = {};
    mode.Property.Set = PROPSETID_VIDCAP_VIDEOCONTROL;
    mode.Property.Id = CSPROPERTY_VIDEOCONTROL_MODE;
    mode.Property.Flags = CSPROPERTY_TYPE_SET;
    mode.StreamIndex = still_pin;
    mode.Mode = CS_VideoControlFlag_Trigger;
    DWORD bytes = 0;
    DWORD armed_started = GetTickCount();
    if (!app.still_armed) {
        DWORD size = app.still.ranges[app.still_range].DataRange.SampleSize;
        if (!arm_buffers(still_pin, app.still, size, 1)) {
            set_status(L"couldn't arm the still pin, see CamTest.log");
            return;
        }
        app.still_armed = true;
        log_line(L"still pin armed in %u ms", GetTickCount() - armed_started);
    }
    DWORD started = GetTickCount();
    if (!DeviceIoControl(app.camera, IOCTL_CS_PROPERTY, &mode, sizeof(mode), &mode, sizeof(mode), &bytes, nullptr)) {
        wchar_t text[80];
        wsprintfW(text, L"trigger failed, error %u", GetLastError());
        set_status(text);
        return;
    }
    set_status(L"Taking photo\x2026");
    UpdateWindow(app.window);
    DWORD waited = WaitForSingleObject(app.still.queue, still_timeout_ms);
    DWORD arrived = GetTickCount();
    CS_MSGQUEUE_BUFFER message;
    DWORD read = 0;
    DWORD flags = 0;
    if (waited != WAIT_OBJECT_0 || !ReadMsgQueue(app.still.queue, &message, sizeof(message), &read, 0, &flags)) {
        wchar_t text[80];
        wsprintfW(text, L"no still after %u ms (wait %u, error %u)", arrived - started, waited, GetLastError());
        set_status(text);
        return;
    }
    CS_STREAM_DESCRIPTOR *descriptor = find_descriptor(app.still, message.pStreamDescriptor);
    if (descriptor == nullptr) {
        set_status(L"still message for an unknown descriptor");
        return;
    }
    const BITMAPINFOHEADER &header = app.still.ranges[app.still_range].VideoInfoHeader.bmiHeader;
    int width = header.biWidth;
    int height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
    DWORD used = descriptor->CsStreamHeader.DataUsed;

    wchar_t path[MAX_PATH];
    wsprintfW(path, L"%s\\still-%dx%d-%u.yv12", app.folder, width, height, GetTickCount());
    DWORD write_started = GetTickCount();
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, descriptor->CsStreamHeader.Data, used, &written, nullptr);
        CloseHandle(file);
    }
    DWORD write_ms = GetTickCount() - write_started;

    app.still_frame = static_cast<const uint8_t *>(descriptor->CsStreamHeader.Data);
    app.still_width = width;
    app.still_height = height;
    show_yv12_frame(app.still_frame, width, height);
    app.showing_still = true;
    app.still_shown_tick = GetTickCount();
    app.still_finished_tick = app.still_shown_tick;
    app.waiting_for_preview = true;
    app.frame_logs = 0;
    CSSTATE still_state = CSSTATE_STOP;
    DWORD returned = 0;
    bool got_state = property(app.still.pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_STATE, CSPROPERTY_TYPE_GET, &still_state, sizeof(still_state), &returned);
    log_line(L"still pin state after the still: %s %d", got_state ? L"" : L"unknown", int(still_state));
    DWORD disarm_started = GetTickCount();
    disarm_buffers(app.still);
    app.still_armed = false;
    log_line(L"still pin stopped in %u ms", GetTickCount() - disarm_started);

    wchar_t text[160];
    wsprintfW(text, L"%dx%d: %u ms to capture, saved in %u ms", width, height, arrived - started, write_ms);
    set_status(text);
    log_line(L"still picture %u drops %u options %x written %u of %u to %s", unsigned(descriptor->CsFrameInfo.PictureNumber), unsigned(descriptor->CsFrameInfo.DropCount), unsigned(descriptor->CsStreamHeader.OptionsFlags), unsigned(written), unsigned(used), path);
}

bool open_still(int width) {
    close_stream(app.still);
    int index = still_range_index(width);
    if (index < 0) {
        log_line(L"no still range %d wide", width);
        return false;
    }
    app.still_range = index;
    CS_DATARANGE_VIDEO range = app.still.ranges[index];
    bool ok = open_stream(still_pin, app.still, range, 1);
    app.still_armed = ok;
    wchar_t text[80];
    wsprintfW(text, ok ? L"Still size %dx%d" : L"Still %d wide failed", width, -range.VideoInfoHeader.bmiHeader.biHeight);
    set_status(text);
    return ok;
}

void change_still(int direction) {
    int current = 0;
    int width = app.still.ranges[app.still_range].VideoInfoHeader.bmiHeader.biWidth;
    for (int index = 0; index < still_width_count; index++) {
        if (still_widths[index] == width) {
            current = index;
        }
    }
    open_still(still_widths[(current + direction + still_width_count) % still_width_count]);
}

void set_zoom(int zoom) {
    if (zoom < 0 || zoom > 3) {
        return;
    }
    CSPROPERTY_CAMERACONTROL_S control = {};
    control.Property.Set = PROPSETID_VIDCAP_CAMERACONTROL;
    control.Property.Id = CSPROPERTY_CAMERACONTROL_ZOOM;
    control.Property.Flags = CSPROPERTY_TYPE_SET;
    control.Value = zoom + 1;
    control.Flags = CSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    control.Capabilities = CSPROPERTY_CAMERACONTROL_FLAGS_AUTO | CSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(app.camera, IOCTL_CS_PROPERTY, &control, sizeof(control), &control, sizeof(control), &bytes, nullptr);
    wchar_t text[80];
    if (ok) {
        app.zoom = zoom;
        wsprintfW(text, L"Zoom %s", zoom_names[zoom]);
    } else {
        wsprintfW(text, L"zoom %d failed, error %u", zoom + 1, GetLastError());
    }
    set_status(text);
}

void start_camera() {
    app.camera = CreateFileW(L"CAM1:", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (app.camera == INVALID_HANDLE_VALUE) {
        wchar_t text[80];
        wsprintfW(text, L"CAM1: open failed, error %u", GetLastError());
        set_status(text);
        return;
    }
    load_ranges(still_pin, app.still);
    if (!load_ranges(preview_pin, app.preview)) {
        set_status(L"no preview formats");
        return;
    }
    int chosen = 0;
    for (int index = 0; index < app.preview.range_count; index++) {
        if (app.preview.ranges[index].VideoInfoHeader.bmiHeader.biWidth == canvas_width) {
            chosen = index;
        }
    }
    app.preview_range = app.preview.ranges[chosen];
    if (!start_preview()) {
        set_status(L"preview setup failed, see CamTest.log");
        return;
    }
    app.second_tick = GetTickCount();
    open_still(still_widths[0]);
}

void stop_camera() {
    close_stream(app.still);
    close_stream(app.preview);
    if (app.camera != INVALID_HANDLE_VALUE && app.camera != nullptr) {
        CloseHandle(app.camera);
        app.camera = INVALID_HANDLE_VALUE;
    }
}

bool create_surface(surface &target, HDC reference, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info = {};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 16;
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = 0xF800;
    info.masks[1] = 0x07E0;
    info.masks[2] = 0x001F;
    target.dc = CreateCompatibleDC(reference);
    void *bits = nullptr;
    target.bitmap = CreateDIBSection(reference, reinterpret_cast<BITMAPINFO *>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
    target.bits = static_cast<uint16_t *>(bits);
    target.width = width;
    target.height = height;
    target.stride = ((width * 2 + 3) & ~3) / 2;
    if (target.bits == nullptr) {
        return false;
    }
    SelectObject(target.dc, target.bitmap);
    SetBkMode(target.dc, TRANSPARENT);
    memset(target.bits, 0, target.stride * 2 * height);
    return true;
}

void rotate_canvas_to_screen() {
    for (int y = 0; y < canvas_height && y < app.screen.width; y++) {
        const uint16_t *source = app.canvas.bits + y * app.canvas.stride;
        uint16_t *target = app.screen.bits + (app.screen.width - 1 - y);
        for (int x = 0; x < canvas_width && x < app.screen.height; x++) {
            target[x * app.screen.stride] = source[x];
        }
    }
}

void screen_to_canvas(int x, int y, int &canvas_x, int &canvas_y) {
    canvas_x = y;
    canvas_y = app.screen.width - 1 - x;
}

void text_with_shadow(const wchar_t *value, RECT area, HFONT font, UINT format) {
    HGDIOBJ previous = SelectObject(app.canvas.dc, font);
    RECT shadow = {area.left + 1, area.top + 1, area.right + 1, area.bottom + 1};
    SetTextColor(app.canvas.dc, RGB(0, 0, 0));
    DrawTextW(app.canvas.dc, value, -1, &shadow, format);
    SetTextColor(app.canvas.dc, RGB(255, 255, 255));
    DrawTextW(app.canvas.dc, value, -1, &area, format);
    SelectObject(app.canvas.dc, previous);
}

RECT shutter_rect() {
    return {canvas_width - shutter_radius * 2 - 10, canvas_height / 2 - shutter_radius, canvas_width - 10, canvas_height / 2 + shutter_radius};
}

RECT close_rect() {
    return {canvas_width - 34, 0, canvas_width, 30};
}

void paint(HDC dc) {
    if (!app.showing_still && app.frames == 0) {
        memset(app.canvas.bits, 0, app.canvas.stride * 2 * canvas_height);
    }
    wchar_t line[200];
    int still_width = app.still.range_count && app.still_range < app.still.range_count ? app.still.ranges[app.still_range].VideoInfoHeader.bmiHeader.biWidth : 0;
    wsprintfW(line, L"%u fps  %u frames  %u dropped  zoom %s  still %d\nimage: rotate %d%s\n%s", app.fps, app.frames, unsigned(app.drops), zoom_names[app.zoom], still_width, app.image_rotation % 360, app.image_rotation >= 360 ? L" flipped" : L"", app.status);
    RECT line_area = {6, 4, canvas_width - 40, 74};
    text_with_shadow(line, line_area, app.small_font, DT_LEFT | DT_WORDBREAK);

    RECT close = close_rect();
    HBRUSH red = CreateSolidBrush(RGB(160, 30, 30));
    FillRect(app.canvas.dc, &close, red);
    DeleteObject(red);
    text_with_shadow(L"X", close, app.large_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT shutter = shutter_rect();
    HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    HBRUSH brush = CreateSolidBrush(app.showing_still ? RGB(40, 170, 70) : RGB(200, 40, 40));
    HGDIOBJ previous_pen = SelectObject(app.canvas.dc, pen);
    HGDIOBJ previous_brush = SelectObject(app.canvas.dc, brush);
    Ellipse(app.canvas.dc, shutter.left, shutter.top, shutter.right, shutter.bottom);
    SelectObject(app.canvas.dc, previous_pen);
    SelectObject(app.canvas.dc, previous_brush);
    DeleteObject(pen);
    DeleteObject(brush);

    RECT help = {6, canvas_height - 20, canvas_width - 6, canvas_height - 2};
    text_with_shadow(L"Rocker: zoom   Left/right: still size   Tap picture: orientation", help, app.small_font, DT_LEFT | DT_SINGLELINE);

    rotate_canvas_to_screen();
    BitBlt(dc, 0, 0, app.screen.width, app.screen.height, app.screen.dc, 0, 0, SRCCOPY);
}

HFONT make_font(int height, bool bold) {
    LOGFONTW font = {};
    font.lfHeight = -height;
    font.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lstrcpyW(font.lfFaceName, L"Tahoma");
    return CreateFontIndirectW(&font);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    static SHACTIVATEINFO activation = {sizeof(activation)};
    switch (message) {
    case WM_CREATE:
        app.window = window;
        return 0;
    case WM_ACTIVATE:
        SHHandleWMActivate(window, wparam, lparam, &activation, 0);
        if (LOWORD(wparam) != WA_INACTIVE) {
            SHFullScreen(window, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
            AllKeys(TRUE);
        } else {
            AllKeys(FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((lparam & 0x40000000) != 0) {
            return 0;
        }
        if (wparam == VK_UP) {
            set_zoom(app.zoom + 1);
        } else if (wparam == VK_DOWN) {
            set_zoom(app.zoom - 1);
        } else if (wparam == VK_LEFT) {
            change_still(-1);
        } else if (wparam == VK_RIGHT) {
            change_still(1);
        } else if (int(wparam) == key_camera || int(wparam) == key_ok || wparam == VK_RETURN) {
            take_still();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int x;
        int y;
        screen_to_canvas(short(LOWORD(lparam)), short(HIWORD(lparam)), x, y);
        POINT tap = {x, y};
        RECT close = close_rect();
        RECT shutter = shutter_rect();
        if (PtInRect(&close, tap)) {
            DestroyWindow(window);
        } else if (PtInRect(&shutter, tap)) {
            take_still();
        } else {
            app.image_rotation = (app.image_rotation + 90) % 720;
            log_line(L"image orientation: rotate %d%s", app.image_rotation % 360, app.image_rotation >= 360 ? L", flipped" : L"");
            if (app.showing_still) {
                app.still_shown_tick = GetTickCount();
                show_yv12_frame(app.still_frame, app.still_width, app.still_height);
            }
            redraw();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint_info;
        HDC dc = BeginPaint(window, &paint_info);
        if (app.canvas.bits && app.screen.bits) {
            paint(dc);
        }
        EndPaint(window, &paint_info);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        AllKeys(FALSE);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    GetModuleFileNameW(nullptr, app.folder, MAX_PATH);
    wchar_t *slash = wcsrchr(app.folder, L'\\');
    if (slash) {
        *slash = 0;
    }
    wchar_t log_path[MAX_PATH];
    wsprintfW(log_path, L"%s\\CamTest.log", app.folder);
    log_open(log_path);
    log_line(L"CamTest starting");
    app.camera = INVALID_HANDLE_VALUE;
    app.image_rotation = 0;
    app.preview.pin = INVALID_HANDLE_VALUE;
    app.still.pin = INVALID_HANDLE_VALUE;

    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"GISmoCamTest";
    RegisterClassW(&window_class);
    app.small_font = make_font(12, false);
    app.large_font = make_font(17, true);

    HWND window = CreateWindowW(L"GISmoCamTest", L"Camera test", WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }
    SHFullScreen(window, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
    MoveWindow(window, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);
    RECT client;
    GetClientRect(window, &client);
    HDC reference = GetDC(window);
    bool surfaces = create_surface(app.screen, reference, client.right, client.bottom) && create_surface(app.canvas, reference, canvas_width, canvas_height);
    ReleaseDC(window, reference);
    log_line(L"screen %dx%d, canvas %dx%d, surfaces %s", client.right, client.bottom, canvas_width, canvas_height, surfaces ? L"ok" : L"failed");
    UpdateWindow(window);
    start_camera();

    MSG message;
    bool running = true;
    DWORD last_tick = GetTickCount();
    while (running) {
        DWORD count = app.preview.queue ? 1 : 0;
        DWORD result = MsgWaitForMultipleObjectsEx(count, count ? &app.preview.queue : nullptr, tick_ms, QS_ALLINPUT, 0);
        if (count && result == WAIT_OBJECT_0) {
            on_preview_frames();
        }
        if (GetTickCount() - last_tick >= tick_ms) {
            last_tick = GetTickCount();
            on_tick();
        }
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    stop_camera();
    log_line(L"exit");
    log_close();
    return 0;
}
