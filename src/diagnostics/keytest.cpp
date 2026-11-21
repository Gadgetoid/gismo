#include <windows.h>
#include <aygshell.h>

namespace {

constexpr int max_keys_per_press = 6;
constexpr int bar_height = 30;
constexpr int button_height = 44;

const wchar_t *const steps[] = {
    L"the camera button",
    L"the side rocker UP",
    L"the side rocker DOWN",
    L"the 5-way pad UP",
    L"the 5-way pad DOWN",
    L"the 5-way pad LEFT",
    L"the 5-way pad RIGHT",
    L"the 5-way pad CENTRE",
    L"HOME (left edge)",
    L"MAIL (right edge)",
    L"the \x2212 button, top left of the pad",
    L"the \x2212 button, top right of the pad",
    L"the WINDOWS button, bottom left of the pad",
    L"OK, bottom right of the pad",
};
constexpr int step_count = sizeof(steps) / sizeof(steps[0]);

struct key_press {
    int count;
    int keys[max_keys_per_press];
    unsigned scans[max_keys_per_press];
};

struct state {
    HWND window;
    HFONT small_font;
    HFONT large_font;
    HFONT huge_font;
    int step;
    key_press current;
    int held;
    key_press results[step_count];
    bool recorded[step_count];
    key_press last;
    bool have_last;
    HANDLE output;
};

state app;

void describe(const key_press &press, wchar_t *text, int text_size) {
    text[0] = 0;
    for (int index = 0; index < press.count; index++) {
        wchar_t part[24];
        wsprintfW(part, index ? L" + %02X" : L"%02X", press.keys[index]);
        if (lstrlenW(text) + lstrlenW(part) < text_size - 1) {
            lstrcatW(text, part);
        }
    }
}

void describe_with_scans(const key_press &press, wchar_t *text, int text_size) {
    text[0] = 0;
    for (int index = 0; index < press.count; index++) {
        wchar_t part[32];
        wsprintfW(part, index ? L" + vk %02X scan %02X" : L"vk %02X scan %02X", press.keys[index], press.scans[index]);
        if (lstrlenW(text) + lstrlenW(part) < text_size - 1) {
            lstrcatW(text, part);
        }
    }
}

void write_line(const wchar_t *line) {
    if (app.output == INVALID_HANDLE_VALUE) {
        return;
    }
    char encoded[256];
    int length = WideCharToMultiByte(CP_UTF8, 0, line, -1, encoded, sizeof(encoded), nullptr, nullptr);
    if (length > 1) {
        DWORD written;
        WriteFile(app.output, encoded, length - 1, &written, nullptr);
        WriteFile(app.output, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(app.output);
    }
}

void record(const key_press &press) {
    wchar_t codes[160];
    describe_with_scans(press, codes, 160);
    wchar_t line[300];
    if (app.step < step_count) {
        wchar_t label[80];
        lstrcpynW(label, steps[app.step], 80);
        for (wchar_t *character = label; *character; character++) {
            if (*character == L'\n') {
                *character = 0;
                break;
            }
        }
        wsprintfW(line, L"%s: %s", label, codes);
        app.results[app.step] = press;
        app.recorded[app.step] = true;
        app.step++;
    } else {
        wsprintfW(line, L"extra: %s", codes);
    }
    write_line(line);
    app.last = press;
    app.have_last = true;
    InvalidateRect(app.window, nullptr, TRUE);
}

RECT skip_rect() {
    RECT client;
    GetClientRect(app.window, &client);
    return {8, client.bottom - button_height, client.right / 2 - 4, client.bottom - 6};
}

RECT back_rect() {
    RECT client;
    GetClientRect(app.window, &client);
    return {client.right / 2 + 4, client.bottom - button_height, client.right - 8, client.bottom - 6};
}

RECT close_rect() {
    RECT client;
    GetClientRect(app.window, &client);
    return {client.right - bar_height, 0, client.right, bar_height};
}

void fill(HDC dc, RECT area, COLORREF colour) {
    HBRUSH brush = CreateSolidBrush(colour);
    FillRect(dc, &area, brush);
    DeleteObject(brush);
}

void text(HDC dc, const wchar_t *value, RECT area, COLORREF colour, HFONT font, UINT format) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, colour);
    DrawTextW(dc, value, -1, &area, format);
    SelectObject(dc, previous);
}

void paint(HDC dc) {
    RECT client;
    GetClientRect(app.window, &client);
    SetBkMode(dc, TRANSPARENT);
    fill(dc, client, RGB(250, 248, 240));
    RECT bar = {0, 0, client.right, bar_height};
    fill(dc, bar, RGB(20, 30, 50));
    RECT title = {8, 0, client.right - bar_height, bar_height};
    text(dc, L"GISmo key test", title, RGB(255, 255, 255), app.small_font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    fill(dc, close_rect(), RGB(160, 30, 30));
    text(dc, L"X", close_rect(), RGB(255, 255, 255), app.large_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    wchar_t heading[200];
    if (app.step < step_count) {
        wsprintfW(heading, L"Step %d of %d\nPress %s", app.step + 1, step_count, steps[app.step]);
    } else {
        lstrcpyW(heading, L"All done!\nResults are in KeyTest.txt.\nPress anything else to see its codes.");
    }
    RECT heading_area = {10, bar_height + 10, client.right - 10, bar_height + 110};
    text(dc, heading, heading_area, RGB(120, 60, 200), app.large_font, DT_CENTER | DT_WORDBREAK);

    if (app.have_last) {
        wchar_t codes[80];
        describe(app.last, codes, 80);
        RECT last_label = {10, bar_height + 112, client.right - 10, bar_height + 130};
        text(dc, L"Last press", last_label, RGB(90, 90, 90), app.small_font, DT_CENTER | DT_SINGLELINE);
        RECT last_area = {10, bar_height + 130, client.right - 10, bar_height + 170};
        text(dc, codes, last_area, RGB(30, 30, 30), app.huge_font, DT_CENTER | DT_SINGLELINE);
    }

    int top = bar_height + 176;
    int first = app.step > 4 ? app.step - 4 : 0;
    for (int index = first; index < app.step && index < step_count && top < client.bottom - button_height - 16; index++) {
        wchar_t label[80];
        lstrcpynW(label, steps[index], 80);
        for (wchar_t *character = label; *character; character++) {
            if (*character == L'\n') {
                *character = 0;
                break;
            }
        }
        wchar_t codes[80];
        if (app.recorded[index]) {
            describe(app.results[index], codes, 80);
        } else {
            lstrcpyW(codes, L"skipped");
        }
        wchar_t line[200];
        wsprintfW(line, L"%s: %s", label, codes);
        RECT row = {8, top, client.right - 8, top + 16};
        text(dc, line, row, RGB(60, 60, 60), app.small_font, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        top += 17;
    }

    fill(dc, skip_rect(), RGB(30, 60, 110));
    text(dc, L"Skip", skip_rect(), RGB(255, 255, 255), app.large_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    fill(dc, back_rect(), RGB(30, 60, 110));
    text(dc, L"Redo last", back_rect(), RGB(255, 255, 255), app.large_font, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

HFONT make_font(int height, bool bold) {
    LOGFONTW font = {};
    font.lfHeight = -height;
    font.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    font.lfQuality = CLEARTYPE_QUALITY;
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
    case WM_SETTINGCHANGE:
        SHHandleWMSettingChange(window, wparam, lparam, &activation);
        return 0;
    case WM_KEYDOWN:
        if ((lparam & 0x40000000) == 0) {
            if (app.held == 0) {
                app.current.count = 0;
            }
            app.held++;
            if (app.current.count < max_keys_per_press) {
                app.current.keys[app.current.count] = int(wparam);
                app.current.scans[app.current.count] = unsigned(lparam >> 16) & 0xFF;
                app.current.count++;
            }
        }
        return 0;
    case WM_KEYUP:
        if (app.held > 0) {
            app.held--;
            if (app.held == 0 && app.current.count > 0) {
                record(app.current);
            }
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT tap = {short(LOWORD(lparam)), short(HIWORD(lparam))};
        RECT close = close_rect();
        RECT skip = skip_rect();
        RECT back = back_rect();
        if (PtInRect(&close, tap)) {
            DestroyWindow(window);
        } else if (PtInRect(&skip, tap) && app.step < step_count) {
            app.recorded[app.step] = false;
            app.step++;
            InvalidateRect(window, nullptr, TRUE);
        } else if (PtInRect(&back, tap) && app.step > 0) {
            app.step--;
            app.recorded[app.step] = false;
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint_info;
        HDC dc = BeginPaint(window, &paint_info);
        paint(dc);
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
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash) {
        *slash = 0;
    }
    lstrcatW(path, L"\\KeyTest.txt");
    app.output = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"GISmoKeyTest";
    RegisterClassW(&window_class);
    app.small_font = make_font(12, false);
    app.large_font = make_font(17, true);
    app.huge_font = make_font(28, true);

    HWND window = CreateWindowW(L"GISmoKeyTest", L"Key test", WS_VISIBLE, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 1;
    }
    SHFullScreen(window, SHFS_HIDETASKBAR | SHFS_HIDESIPBUTTON | SHFS_HIDESTARTICON);
    MoveWindow(window, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), TRUE);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (app.output != INVALID_HANDLE_VALUE) {
        CloseHandle(app.output);
    }
    return 0;
}
