#include "camera.h"

#include <cs.h>
#include <csmedia.h>
#include <msgqueue.h>

#include <stdlib.h>
#include <string.h>

#include "log.h"

namespace gismo {

constexpr int camera_buffers = 2;

class camera_driver {
public:
    camera_driver();
    ~camera_driver();

    bool start(HWND notify_window, UINT notify_message);
    void stop();

    bool running() const {
        return active;
    }

    bool copy_frame(uint16_t *target);
    bool set_zoom(int level);

    int zoom() const {
        return zoom_level;
    }

private:
    static DWORD WINAPI thread_entry(LPVOID parameter);
    void run();
    bool open_preview();
    void close_preview();
    bool property(HANDLE target, const GUID &set, ULONG id, ULONG flags, void *output, DWORD output_size);
    bool buffer_command(DWORD command, CS_STREAM_DESCRIPTOR &descriptor);
    CS_STREAM_DESCRIPTOR *find_descriptor(PCS_STREAM_DESCRIPTOR reported);

    HANDLE device;
    HANDLE pin;
    HANDLE queue;
    HANDLE thread;
    HWND window;
    UINT message;
    volatile bool stopping;
    bool active;
    int zoom_level;
    CRITICAL_SECTION lock;
    uint16_t *latest;
    bool have_frame;
    CS_DATARANGE_VIDEO range;
    CS_STREAM_DESCRIPTOR descriptors[camera_buffers];
    int descriptor_count;
};

namespace {

constexpr ULONG preview_pin = 2;
constexpr DWORD frame_wait_ms = 500;



}

camera_driver::camera_driver() : device(INVALID_HANDLE_VALUE), pin(INVALID_HANDLE_VALUE), queue(nullptr), thread(nullptr), window(nullptr), message(0), stopping(false), active(false), zoom_level(1), latest(nullptr), have_frame(false), descriptor_count(0) {
    InitializeCriticalSection(&lock);
    memset(&range, 0, sizeof(range));
    memset(descriptors, 0, sizeof(descriptors));
}

camera_driver::~camera_driver() {
    stop();
    DeleteCriticalSection(&lock);
}

bool camera_driver::property(HANDLE target, const GUID &set, ULONG id, ULONG flags, void *output, DWORD output_size) {
    CSPROPERTY request = {};
    request.Set = set;
    request.Id = id;
    request.Flags = flags;
    DWORD bytes = 0;
    return DeviceIoControl(target, IOCTL_CS_PROPERTY, &request, sizeof(request), output, output_size, &bytes, nullptr) != FALSE;
}

bool camera_driver::buffer_command(DWORD command, CS_STREAM_DESCRIPTOR &descriptor) {
    DWORD bytes = 0;
    if (!DeviceIoControl(pin, IOCTL_CS_BUFFERS, &command, sizeof(command), &descriptor, sizeof(descriptor), &bytes, nullptr)) {
        log_line(L"camera: buffer command %u failed, error %u", unsigned(command), GetLastError());
        return false;
    }
    return true;
}

CS_STREAM_DESCRIPTOR *camera_driver::find_descriptor(PCS_STREAM_DESCRIPTOR reported) {
    uintptr_t value = reinterpret_cast<uintptr_t>(reported) & 0x01FFFFFF;
    for (int index = 0; index < descriptor_count; index++) {
        if ((reinterpret_cast<uintptr_t>(&descriptors[index]) & 0x01FFFFFF) == value) {
            return &descriptors[index];
        }
    }
    return nullptr;
}

bool camera_driver::open_preview() {
    CSP_PIN ranges_request = {};
    ranges_request.Property.Set = CSPROPSETID_Pin;
    ranges_request.Property.Id = CSPROPERTY_PIN_DATARANGES;
    ranges_request.Property.Flags = CSPROPERTY_TYPE_GET;
    ranges_request.PinId = preview_pin;
    DWORD needed = 0;
    CSMULTIPLE_ITEM probe = {};
    DeviceIoControl(device, IOCTL_CS_PROPERTY, &ranges_request, sizeof(ranges_request), &probe, sizeof(probe), &needed, nullptr);
    if (needed < sizeof(CSMULTIPLE_ITEM)) {
        log_line(L"camera: no preview ranges, error %u", GetLastError());
        return false;
    }
    uint8_t *buffer = static_cast<uint8_t *>(malloc(needed));
    DWORD returned = 0;
    bool found = false;
    if (buffer && DeviceIoControl(device, IOCTL_CS_PROPERTY, &ranges_request, sizeof(ranges_request), buffer, needed, &returned, nullptr)) {
        const CSMULTIPLE_ITEM *items = reinterpret_cast<const CSMULTIPLE_ITEM *>(buffer);
        int count = int((items->Size - sizeof(CSMULTIPLE_ITEM)) / sizeof(CS_DATARANGE_VIDEO));
        for (int index = 0; index < count; index++) {
            const CS_DATARANGE_VIDEO *candidate = reinterpret_cast<const CS_DATARANGE_VIDEO *>(buffer + sizeof(CSMULTIPLE_ITEM) + index * sizeof(CS_DATARANGE_VIDEO));
            if (candidate->VideoInfoHeader.bmiHeader.biWidth == camera_width && memcmp(&candidate->DataRange.SubFormat, &MEDIASUBTYPE_RGB565, sizeof(GUID)) == 0) {
                memcpy(&range, candidate, sizeof(range));
                found = true;
            }
        }
    }
    free(buffer);
    if (!found) {
        log_line(L"camera: no 320x240 RGB565 preview range");
        return false;
    }

    MSGQUEUEOPTIONS options = {};
    options.dwSize = sizeof(options);
    options.dwMaxMessages = 8;
    options.cbMaxMessage = sizeof(CS_MSGQUEUE_BUFFER);
    options.bReadAccess = TRUE;
    queue = CreateMsgQueue(nullptr, &options);
    pin = CreateFileW(L"PIN1:", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (queue == nullptr || pin == INVALID_HANDLE_VALUE) {
        log_line(L"camera: queue or PIN1: failed, error %u", GetLastError());
        return false;
    }

    CSPROPERTY_STREAMEX_S instantiate = {};
    instantiate.CsPin.Property.Set = CSPROPSETID_StreamEx;
    instantiate.CsPin.Property.Id = CSPROPERTY_STREAMEX_INIT;
    instantiate.CsPin.Property.Flags = CSPROPERTY_TYPE_SET;
    instantiate.CsPin.PinId = preview_pin;
    instantiate.hMsgQueue = queue;
    DWORD bytes = 0;
    if (!DeviceIoControl(pin, IOCTL_STREAM_INSTANTIATE, &instantiate, sizeof(instantiate), nullptr, 0, &bytes, nullptr)) {
        log_line(L"camera: instantiate failed, error %u", GetLastError());
        return false;
    }

    static CS_DATAFORMAT_VIDEOINFOHEADER format;
    memset(&format, 0, sizeof(format));
    memcpy(&format.DataFormat, &range.DataRange, sizeof(CSDATAFORMAT));
    format.DataFormat.FormatSize = sizeof(format);
    memcpy(&format.VideoInfoHeader, &range.VideoInfoHeader, sizeof(CS_VIDEOINFOHEADER));
    if (!property(pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_DATAFORMAT, CSPROPERTY_TYPE_SET, &format, sizeof(format))) {
        log_line(L"camera: data format failed, error %u", GetLastError());
        return false;
    }
    CSSTATE pause = CSSTATE_PAUSE;
    if (!property(pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_STATE, CSPROPERTY_TYPE_SET, &pause, sizeof(pause))) {
        log_line(L"camera: pause failed, error %u", GetLastError());
        return false;
    }

    DWORD size = range.DataRange.SampleSize;
    for (int index = 0; index < camera_buffers; index++) {
        CS_STREAM_DESCRIPTOR &descriptor = descriptors[index];
        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.CsStreamHeader.Size = sizeof(CSSTREAM_HEADER);
        descriptor.CsStreamHeader.FrameExtent = size;
        descriptor.CsFrameInfo.ExtendedHeaderSize = sizeof(CS_FRAME_INFO);
        descriptor.CsStreamHeader.Data = malloc(size);
        if (descriptor.CsStreamHeader.Data == nullptr || !buffer_command(CS_ALLOCATE, descriptor)) {
            free(descriptor.CsStreamHeader.Data);
            descriptor.CsStreamHeader.Data = nullptr;
            return false;
        }
        descriptor_count = index + 1;
        if (!buffer_command(CS_ENQUEUE, descriptor)) {
            return false;
        }
    }

    CSSTATE run_state = CSSTATE_RUN;
    if (!property(pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_STATE, CSPROPERTY_TYPE_SET, &run_state, sizeof(run_state))) {
        log_line(L"camera: run failed, error %u", GetLastError());
        return false;
    }
    return true;
}

void camera_driver::close_preview() {
    if (pin != INVALID_HANDLE_VALUE) {
        CSSTATE stop_state = CSSTATE_STOP;
        property(pin, CSPROPSETID_Connection, CSPROPERTY_CONNECTION_STATE, CSPROPERTY_TYPE_SET, &stop_state, sizeof(stop_state));
        CS_MSGQUEUE_BUFFER drained;
        DWORD read;
        DWORD flags;
        while (queue && ReadMsgQueue(queue, &drained, sizeof(drained), &read, 0, &flags)) {
        }
        for (int index = 0; index < descriptor_count; index++) {
            buffer_command(CS_DEALLOCATE, descriptors[index]);
            free(descriptors[index].CsStreamHeader.Data);
            descriptors[index].CsStreamHeader.Data = nullptr;
        }
        descriptor_count = 0;
        CloseHandle(pin);
        pin = INVALID_HANDLE_VALUE;
    }
    if (queue) {
        CloseMsgQueue(queue);
        queue = nullptr;
    }
}

bool camera_driver::start(HWND notify_window, UINT notify_message) {
    if (active) {
        return true;
    }
    window = notify_window;
    message = notify_message;
    if (latest == nullptr) {
        latest = static_cast<uint16_t *>(malloc(camera_width * camera_height * sizeof(uint16_t)));
        if (latest == nullptr) {
            return false;
        }
    }
    DWORD started = GetTickCount();
    device = CreateFileW(L"CAM1:", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        log_line(L"camera: CAM1: open failed, error %u", GetLastError());
        return false;
    }
    if (!open_preview()) {
        close_preview();
        CloseHandle(device);
        device = INVALID_HANDLE_VALUE;
        return false;
    }
    have_frame = false;
    stopping = false;
    active = true;
    set_zoom(zoom_level);
    thread = CreateThread(nullptr, 0, thread_entry, this, 0, nullptr);
    log_line(L"camera: started in %u ms", GetTickCount() - started);
    return true;
}

void camera_driver::stop() {
    if (!active) {
        return;
    }
    stopping = true;
    if (thread) {
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
        thread = nullptr;
    }
    close_preview();
    if (device != INVALID_HANDLE_VALUE) {
        CloseHandle(device);
        device = INVALID_HANDLE_VALUE;
    }
    active = false;
    log_line(L"camera: stopped");
}

DWORD WINAPI camera_driver::thread_entry(LPVOID parameter) {
    static_cast<camera_driver *>(parameter)->run();
    return 0;
}

void camera_driver::run() {
    while (!stopping) {
        if (WaitForSingleObject(queue, frame_wait_ms) != WAIT_OBJECT_0) {
            continue;
        }
        CS_MSGQUEUE_BUFFER received;
        DWORD read = 0;
        DWORD flags = 0;
        bool updated = false;
        while (ReadMsgQueue(queue, &received, sizeof(received), &read, 0, &flags)) {
            CS_STREAM_DESCRIPTOR *descriptor = find_descriptor(received.pStreamDescriptor);
            if (descriptor == nullptr) {
                continue;
            }
            EnterCriticalSection(&lock);
            memcpy(latest, descriptor->CsStreamHeader.Data, camera_width * camera_height * sizeof(uint16_t));
            have_frame = true;
            LeaveCriticalSection(&lock);
            buffer_command(CS_ENQUEUE, *descriptor);
            updated = true;
        }
        if (updated) {
            PostMessageW(window, message, 0, 0);
        }
    }
}

bool camera_driver::copy_frame(uint16_t *target) {
    EnterCriticalSection(&lock);
    bool copied = have_frame;
    if (copied) {
        memcpy(target, latest, camera_width * camera_height * sizeof(uint16_t));
    }
    LeaveCriticalSection(&lock);
    return copied;
}

bool camera_driver::set_zoom(int level) {
    if (level < 1 || level > camera_max_zoom || device == INVALID_HANDLE_VALUE) {
        return false;
    }
    CSPROPERTY_CAMERACONTROL_S control = {};
    control.Property.Set = PROPSETID_VIDCAP_CAMERACONTROL;
    control.Property.Id = CSPROPERTY_CAMERACONTROL_ZOOM;
    control.Property.Flags = CSPROPERTY_TYPE_SET;
    control.Value = level;
    control.Flags = CSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    control.Capabilities = CSPROPERTY_CAMERACONTROL_FLAGS_AUTO | CSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    DWORD bytes = 0;
    if (!DeviceIoControl(device, IOCTL_CS_PROPERTY, &control, sizeof(control), &control, sizeof(control), &bytes, nullptr)) {
        log_line(L"camera: zoom %d failed, error %u", level, GetLastError());
        return false;
    }
    zoom_level = level;
    return true;
}

camera::camera() : driver(new camera_driver()) {
}

camera::~camera() {
    delete driver;
}

bool camera::available() const {
    return true;
}

bool camera::start(HWND notify_window, UINT notify_message) {
    return driver->start(notify_window, notify_message);
}

void camera::stop() {
    driver->stop();
}

bool camera::running() const {
    return driver->running();
}

bool camera::copy_frame(uint16_t *target) {
    return driver->copy_frame(target);
}

bool camera::set_zoom(int level) {
    return driver->set_zoom(level);
}

int camera::zoom() const {
    return driver->zoom();
}

}
