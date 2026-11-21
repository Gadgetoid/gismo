#include <windows.h>

#include <dirent.h>
#include <fnmatch.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "shim.h"

namespace {

enum class object_kind {
    dc,
    bitmap,
    brush,
    font,
};

struct font_face {
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    bool loaded = false;
};

struct glyph {
    std::vector<unsigned char> alpha;
    int width = 0;
    int height = 0;
    int x_offset = 0;
    int y_offset = 0;
    int advance = 0;
};

struct search {
    std::vector<std::string> names;
    std::vector<bool> folders;
    size_t next = 0;
};

font_face faces[2];
DWORD tick = 1000000;
SYSTEMTIME clock_time = {2026, 11, 6, 14, 10, 30, 0, 0};
std::string card_folder = ".";
std::set<FILE *> open_files;

font_face &face(bool bold) {
    font_face &chosen = faces[bold ? 1 : 0];
    if (!chosen.loaded) {
        const char *chosen_path = getenv(bold ? "GISMO_BOLD_FONT" : "GISMO_FONT");
        const char *folder = getenv("GISMO_FONTS");
        std::string path = chosen_path ? chosen_path : std::string(folder ? folder : "/System/Library/Fonts/Supplemental") + (bold ? "/Tahoma Bold.ttf" : "/Tahoma.ttf");
        FILE *file = fopen(path.c_str(), "rb");
        if (file == nullptr) {
            fprintf(stderr, "screens: can't open %s (set GISMO_FONTS, or GISMO_FONT and GISMO_BOLD_FONT)\n", path.c_str());
            exit(1);
        }
        fseek(file, 0, SEEK_END);
        chosen.data.resize(ftell(file));
        fseek(file, 0, SEEK_SET);
        fread(chosen.data.data(), 1, chosen.data.size(), file);
        fclose(file);
        stbtt_InitFont(&chosen.info, chosen.data.data(), stbtt_GetFontOffsetForIndex(chosen.data.data(), 0));
        chosen.loaded = true;
    }
    return chosen;
}

std::string narrow_path(const wchar_t *path) {
    std::string result;
    for (const wchar_t *character = path; *character; character++) {
        wchar_t value = *character == L'\\' ? L'/' : *character;
        if (value < 0x80) {
            result += char(value);
        } else if (value < 0x800) {
            result += char(0xC0 | (value >> 6));
            result += char(0x80 | (value & 0x3F));
        } else if (value < 0x10000) {
            result += char(0xE0 | (value >> 12));
            result += char(0x80 | ((value >> 6) & 0x3F));
            result += char(0x80 | (value & 0x3F));
        } else {
            result += char(0xF0 | (value >> 18));
            result += char(0x80 | ((value >> 12) & 0x3F));
            result += char(0x80 | ((value >> 6) & 0x3F));
            result += char(0x80 | (value & 0x3F));
        }
    }
    return result;
}

uint16_t to_565(COLORREF colour) {
    return uint16_t((GetRValue(colour) >> 3) << 11 | (GetGValue(colour) >> 2) << 5 | GetBValue(colour) >> 3);
}

uint16_t blend(uint16_t under, COLORREF colour, int alpha) {
    int red = ((under >> 11) & 0x1F) << 3;
    int green = ((under >> 5) & 0x3F) << 2;
    int blue = (under & 0x1F) << 3;
    red += (GetRValue(colour) - red) * alpha / 255;
    green += (GetGValue(colour) - green) * alpha / 255;
    blue += (GetBValue(colour) - blue) * alpha / 255;
    return uint16_t((red >> 3) << 11 | (green >> 2) << 5 | blue >> 3);
}

}

struct shim_object {
    object_kind kind;
    int width = 0;
    int height = 0;
    uint16_t *bits = nullptr;
    COLORREF colour = 0;
    bool bold = false;
    int pixel_height = 0;
    float scale = 0;
    int ascent = 0;
    int line_height = 0;
    std::map<int, glyph> glyphs;
    shim_object *bitmap = nullptr;
    shim_object *font = nullptr;
    COLORREF text_colour = 0;
};

namespace {

const glyph &glyph_for(shim_object *font, int codepoint) {
    auto found = font->glyphs.find(codepoint);
    if (found != font->glyphs.end()) {
        return found->second;
    }
    stbtt_fontinfo &info = face(font->bold).info;
    glyph &made = font->glyphs[codepoint];
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&info, codepoint, &advance, &bearing);
    made.advance = int(lroundf(advance * font->scale));
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&info, codepoint, font->scale, font->scale, &x0, &y0, &x1, &y1);
    made.width = x1 - x0;
    made.height = y1 - y0;
    made.x_offset = x0;
    made.y_offset = y0;
    if (made.width > 0 && made.height > 0) {
        made.alpha.resize(made.width * made.height);
        stbtt_MakeCodepointBitmap(&info, made.alpha.data(), made.width, made.height, made.width, font->scale, font->scale, codepoint);
    }
    return made;
}

int text_width(shim_object *font, const std::wstring &text) {
    int width = 0;
    for (wchar_t character : text) {
        width += glyph_for(font, character).advance;
    }
    return width;
}

std::wstring with_ellipsis(shim_object *font, std::wstring text, int width) {
    if (text_width(font, text) <= width) {
        return text;
    }
    while (!text.empty() && text_width(font, text + L"...") > width) {
        text.pop_back();
    }
    return text + L"...";
}

std::vector<std::wstring> layout_lines(shim_object *font, const std::wstring &text, int width, UINT format) {
    std::vector<std::wstring> paragraphs;
    std::wstring current;
    for (wchar_t character : text) {
        if (character == L'\r') {
            continue;
        }
        if (character == L'\n' && !(format & DT_SINGLELINE)) {
            paragraphs.push_back(current);
            current.clear();
        } else {
            current += character == L'\n' ? L' ' : character;
        }
    }
    paragraphs.push_back(current);
    if (!(format & DT_WORDBREAK) || (format & DT_SINGLELINE)) {
        return paragraphs;
    }
    std::vector<std::wstring> lines;
    for (const std::wstring &paragraph : paragraphs) {
        std::wstring line;
        size_t position = 0;
        while (position <= paragraph.size()) {
            size_t space = paragraph.find(L' ', position);
            std::wstring word = paragraph.substr(position, space == std::wstring::npos ? std::wstring::npos : space - position);
            std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && text_width(font, candidate) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
            if (space == std::wstring::npos) {
                break;
            }
            position = space + 1;
        }
        lines.push_back(line);
    }
    return lines;
}

void draw_line(shim_object *dc, const std::wstring &line, int x, int baseline, const RECT &clip) {
    shim_object *bitmap = dc->bitmap;
    for (wchar_t character : line) {
        const glyph &shape = glyph_for(dc->font, character);
        for (int row = 0; row < shape.height; row++) {
            int y = baseline + shape.y_offset + row;
            if (y < clip.top || y >= clip.bottom || y < 0 || y >= bitmap->height) {
                continue;
            }
            for (int column = 0; column < shape.width; column++) {
                int pixel_x = x + shape.x_offset + column;
                int alpha = shape.alpha[row * shape.width + column];
                if (alpha == 0 || pixel_x < clip.left || pixel_x >= clip.right || pixel_x < 0 || pixel_x >= bitmap->width) {
                    continue;
                }
                uint16_t &target = bitmap->bits[y * bitmap->width + pixel_x];
                target = blend(target, dc->text_colour, alpha);
            }
        }
        x += shape.advance;
    }
}

}

void shim_set_tick(DWORD value) {
    tick = value;
}

void shim_set_card_folder(const char *folder) {
    card_folder = folder;
}

const uint16_t *shim_bitmap_bits(HBITMAP bitmap) {
    return bitmap->bits;
}

DWORD GetTickCount() {
    return tick;
}

DWORD GetLastError() {
    return 0;
}

void Sleep(DWORD) {
}

BOOL MessageBeep(UINT) {
    return TRUE;
}

int GetSystemMetrics(int index) {
    return index == SM_CXSCREEN ? 240 : 320;
}

void GetSystemTime(SYSTEMTIME *time) {
    *time = clock_time;
}

void GetLocalTime(SYSTEMTIME *time) {
    *time = clock_time;
}

BOOL SetSystemTime(const SYSTEMTIME *time) {
    clock_time = *time;
    return TRUE;
}

BOOL SystemTimeToFileTime(const SYSTEMTIME *time, FILETIME *file_time) {
    int year = time->wYear;
    int month = time->wMonth;
    int day = time->wDay;
    year -= month <= 2;
    int era = year / 400;
    int year_of_era = year - era * 400;
    int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    int64_t days = int64_t(era) * 146097 + day_of_era - 719468 + 134774;
    uint64_t value = ((uint64_t(days) * 86400 + time->wHour * 3600 + time->wMinute * 60 + time->wSecond) * 1000 + time->wMilliseconds) * 10000;
    file_time->dwLowDateTime = DWORD(value);
    file_time->dwHighDateTime = DWORD(value >> 32);
    return TRUE;
}

void SystemIdleTimerReset() {
}

HDC CreateCompatibleDC(HDC) {
    shim_object *dc = new shim_object;
    dc->kind = object_kind::dc;
    return dc;
}

BOOL DeleteDC(HDC dc) {
    delete dc;
    return TRUE;
}

HBITMAP CreateDIBSection(HDC, const BITMAPINFO *info, UINT, void **bits, HANDLE, DWORD) {
    shim_object *bitmap = new shim_object;
    bitmap->kind = object_kind::bitmap;
    bitmap->width = info->bmiHeader.biWidth;
    bitmap->height = abs(info->bmiHeader.biHeight);
    bitmap->bits = static_cast<uint16_t *>(calloc(size_t(bitmap->width) * bitmap->height, sizeof(uint16_t)));
    *bits = bitmap->bits;
    return bitmap;
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    if (dc == nullptr || object == nullptr) {
        return nullptr;
    }
    shim_object *previous = nullptr;
    if (object->kind == object_kind::bitmap) {
        previous = dc->bitmap;
        dc->bitmap = object;
    } else if (object->kind == object_kind::font) {
        previous = dc->font;
        dc->font = object;
    }
    return previous;
}

BOOL DeleteObject(HGDIOBJ object) {
    if (object && object->kind == object_kind::bitmap) {
        free(object->bits);
    }
    delete object;
    return TRUE;
}

HBRUSH CreateSolidBrush(COLORREF colour) {
    shim_object *brush = new shim_object;
    brush->kind = object_kind::brush;
    brush->colour = colour;
    return brush;
}

int FillRect(HDC dc, const RECT *area, HBRUSH brush) {
    shim_object *bitmap = dc->bitmap;
    if (bitmap == nullptr) {
        return 0;
    }
    uint16_t value = to_565(brush->colour);
    int left = area->left < 0 ? 0 : area->left;
    int top = area->top < 0 ? 0 : area->top;
    int right = area->right > bitmap->width ? bitmap->width : area->right;
    int bottom = area->bottom > bitmap->height ? bitmap->height : area->bottom;
    for (int y = top; y < bottom; y++) {
        for (int x = left; x < right; x++) {
            bitmap->bits[y * bitmap->width + x] = value;
        }
    }
    return 1;
}

COLORREF SetTextColor(HDC dc, COLORREF colour) {
    COLORREF previous = dc->text_colour;
    dc->text_colour = colour;
    return previous;
}

int SetBkMode(HDC, int) {
    return TRANSPARENT;
}

HFONT CreateFontIndirectW(const LOGFONTW *description) {
    shim_object *font = new shim_object;
    font->kind = object_kind::font;
    font->bold = description->lfWeight >= FW_BOLD;
    font->pixel_height = abs(description->lfHeight);
    stbtt_fontinfo &info = face(font->bold).info;
    font->scale = stbtt_ScaleForMappingEmToPixels(&info, float(font->pixel_height));
    int win_ascent = 0;
    int win_descent = 0;
    int line_gap = 0;
    if (!stbtt_GetFontVMetricsOS2(&info, &win_ascent, &win_descent, &line_gap)) {
        stbtt_GetFontVMetrics(&info, &win_ascent, &win_descent, &line_gap);
    }
    font->ascent = int(lroundf(win_ascent * font->scale));
    font->line_height = font->ascent + int(lroundf(fabsf(float(win_descent)) * font->scale));
    return font;
}

int DrawTextW(HDC dc, const wchar_t *text, int length, RECT *area, UINT format) {
    if (dc->font == nullptr || dc->bitmap == nullptr) {
        return 0;
    }
    std::wstring value(text, length < 0 ? wcslen(text) : size_t(length));
    int width = area->right - area->left;
    std::vector<std::wstring> lines = layout_lines(dc->font, value, width, format);
    if (format & DT_END_ELLIPSIS) {
        for (std::wstring &line : lines) {
            line = with_ellipsis(dc->font, line, width);
        }
    }
    int line_height = dc->font->line_height;
    int total = int(lines.size()) * line_height;
    if (format & DT_CALCRECT) {
        int widest = 0;
        for (const std::wstring &line : lines) {
            int measured = text_width(dc->font, line);
            widest = measured > widest ? measured : widest;
        }
        area->right = area->left + widest;
        area->bottom = area->top + total;
        return total;
    }
    int top = area->top;
    if (format & DT_SINGLELINE) {
        if (format & DT_VCENTER) {
            top = area->top + (area->bottom - area->top - line_height) / 2;
        } else if (format & DT_BOTTOM) {
            top = area->bottom - line_height;
        }
    }
    RECT clip = (format & DT_NOCLIP) ? RECT{-100000, -100000, 100000, 100000} : *area;
    for (size_t index = 0; index < lines.size(); index++) {
        int line_width = text_width(dc->font, lines[index]);
        int x = area->left;
        if (format & DT_CENTER) {
            x = area->left + (width - line_width) / 2;
        } else if (format & DT_RIGHT) {
            x = area->right - line_width;
        }
        draw_line(dc, lines[index], x, top + int(index) * line_height + dc->font->ascent, clip);
    }
    return total;
}

BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) {
    return TRUE;
}

HDC GetDC(HWND) {
    return nullptr;
}

int ReleaseDC(HWND, HDC) {
    return 1;
}

HDC BeginPaint(HWND, PAINTSTRUCT *paint) {
    paint->hdc = nullptr;
    return nullptr;
}

BOOL EndPaint(HWND, const PAINTSTRUCT *) {
    return TRUE;
}

BOOL GetClientRect(HWND, RECT *area) {
    *area = {0, 0, 240, 320};
    return TRUE;
}

BOOL InvalidateRect(HWND, const RECT *, BOOL) {
    return TRUE;
}

BOOL UpdateWindow(HWND) {
    return TRUE;
}

HWND SetCapture(HWND window) {
    return window;
}

BOOL ReleaseCapture() {
    return TRUE;
}

BOOL PtInRect(const RECT *area, POINT point) {
    return point.x >= area->left && point.x < area->right && point.y >= area->top && point.y < area->bottom;
}

UINT_PTR SetTimer(HWND, UINT_PTR id, UINT, void *) {
    return id;
}

BOOL KillTimer(HWND, UINT_PTR) {
    return TRUE;
}

BOOL MoveWindow(HWND, int, int, int, int, BOOL) {
    return TRUE;
}

BOOL SetForegroundWindow(HWND) {
    return TRUE;
}

HWND FindWindowW(LPCWSTR, LPCWSTR) {
    return nullptr;
}

BOOL DestroyWindow(HWND) {
    return TRUE;
}

void PostQuitMessage(int) {
}

LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) {
    return 0;
}

BOOL SHFullScreen(HWND, DWORD) {
    return TRUE;
}

BOOL AllKeys(BOOL) {
    return TRUE;
}

HANDLE SetPowerRequirement(LPCWSTR, int, unsigned long, void *, unsigned long) {
    return nullptr;
}

DWORD ReleasePowerRequirement(HANDLE) {
    return 0;
}

DWORD GetModuleFileNameW(HINSTANCE, wchar_t *name, DWORD size) {
    std::string path = card_folder + "/GISmo.exe";
    for (char &character : path) {
        character = character == '/' ? '\\' : character;
    }
    return DWORD(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, name, int(size)) - 1);
}

BOOL CreateDirectoryW(LPCWSTR path, void *) {
    return mkdir(narrow_path(path).c_str(), 0755) == 0;
}

DWORD GetFileAttributesW(LPCWSTR path) {
    struct stat status;
    if (stat(narrow_path(path).c_str(), &status) != 0) {
        return 0xFFFFFFFF;
    }
    return S_ISDIR(status.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

static void fill_found(search *results, WIN32_FIND_DATAW *found) {
    const std::string &name = results->names[results->next];
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, found->cFileName, MAX_PATH);
    found->dwFileAttributes = results->folders[results->next] ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    results->next++;
}

HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *found) {
    std::string full = narrow_path(pattern);
    size_t slash = full.rfind('/');
    std::string folder = slash == std::string::npos ? "." : full.substr(0, slash);
    std::string wildcard = slash == std::string::npos ? full : full.substr(slash + 1);
    DIR *directory = opendir(folder.c_str());
    if (directory == nullptr) {
        return INVALID_HANDLE_VALUE;
    }
    search *results = new search;
    std::vector<std::pair<std::string, bool>> entries;
    while (dirent *entry = readdir(directory)) {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || fnmatch(wildcard.c_str(), name.c_str(), FNM_CASEFOLD) != 0) {
            continue;
        }
        struct stat status;
        bool is_folder = stat((folder + "/" + name).c_str(), &status) == 0 && S_ISDIR(status.st_mode);
        entries.push_back({name, is_folder});
    }
    closedir(directory);
    std::sort(entries.begin(), entries.end());
    for (const auto &[name, is_folder] : entries) {
        results->names.push_back(name);
        results->folders.push_back(is_folder);
    }
    if (results->names.empty()) {
        delete results;
        return INVALID_HANDLE_VALUE;
    }
    fill_found(results, found);
    return results;
}

BOOL FindNextFileW(HANDLE handle, WIN32_FIND_DATAW *found) {
    search *results = static_cast<search *>(handle);
    if (results->next >= results->names.size()) {
        return FALSE;
    }
    fill_found(results, found);
    return TRUE;
}

BOOL FindClose(HANDLE handle) {
    delete static_cast<search *>(handle);
    return TRUE;
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD, void *, DWORD, DWORD, HANDLE) {
    FILE *file = fopen(narrow_path(path).c_str(), (access & GENERIC_WRITE) ? "wb" : "rb");
    if (file == nullptr) {
        return INVALID_HANDLE_VALUE;
    }
    open_files.insert(file);
    return file;
}

BOOL ReadFile(HANDLE file, void *data, DWORD length, DWORD *read, void *) {
    *read = DWORD(fread(data, 1, length, static_cast<FILE *>(file)));
    return TRUE;
}

DWORD SetFilePointer(HANDLE file, LONG distance, LONG *, DWORD) {
    fseek(static_cast<FILE *>(file), distance, SEEK_SET);
    return DWORD(distance);
}

BOOL WriteFile(HANDLE file, const void *data, DWORD length, DWORD *written, void *) {
    *written = DWORD(fwrite(data, 1, length, static_cast<FILE *>(file)));
    return *written == length;
}

BOOL CloseHandle(HANDLE handle) {
    FILE *file = static_cast<FILE *>(handle);
    if (open_files.erase(file)) {
        fclose(file);
    }
    return TRUE;
}

int MultiByteToWideChar(UINT, DWORD, const char *text, int length, wchar_t *output, int output_size) {
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(text);
    size_t end = length < 0 ? strlen(text) + 1 : size_t(length);
    int written = 0;
    for (size_t position = 0; position < end;) {
        uint32_t value = bytes[position];
        int extra = value >= 0xF0 ? 3 : value >= 0xE0 ? 2 : value >= 0xC0 ? 1 : 0;
        value &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
        position++;
        for (int count = 0; count < extra && position < end; count++, position++) {
            value = value << 6 | (bytes[position] & 0x3F);
        }
        if (output_size) {
            if (written >= output_size) {
                return 0;
            }
            output[written] = wchar_t(value);
        }
        written++;
    }
    return written;
}

int WideCharToMultiByte(UINT, DWORD, const wchar_t *text, int length, char *output, int output_size, const char *, BOOL *) {
    std::wstring value(text, length < 0 ? wcslen(text) + 1 : size_t(length));
    std::string encoded;
    for (wchar_t character : value) {
        wchar_t single[2] = {character == 0 ? L' ' : character, 0};
        std::string part = character == 0 ? std::string(1, '\0') : narrow_path(single);
        encoded += part;
    }
    if (output_size == 0) {
        return int(encoded.size());
    }
    if (int(encoded.size()) > output_size) {
        return 0;
    }
    memcpy(output, encoded.data(), encoded.size());
    return int(encoded.size());
}

int wsprintfW(wchar_t *output, const wchar_t *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    std::wstring result;
    for (const wchar_t *character = format; *character; character++) {
        if (*character != L'%') {
            result += *character;
            continue;
        }
        std::wstring spec = L"%";
        character++;
        while (*character && wcschr(L"-+ #0123456789.", *character)) {
            spec += *character++;
        }
        bool is_long = false;
        while (*character == L'l' || *character == L'h') {
            is_long = is_long || *character == L'l';
            character++;
        }
        wchar_t buffer[512];
        switch (*character) {
        case L'%':
            result += L'%';
            continue;
        case L'd':
        case L'i':
            swprintf(buffer, 512, (spec + L"d").c_str(), is_long ? int(va_arg(arguments, long)) : va_arg(arguments, int));
            break;
        case L'u':
        case L'x':
        case L'X':
            swprintf(buffer, 512, (spec + *character).c_str(), is_long ? unsigned(va_arg(arguments, unsigned long)) : va_arg(arguments, unsigned));
            break;
        case L'c':
            buffer[0] = wchar_t(va_arg(arguments, int));
            buffer[1] = 0;
            break;
        case L's':
            swprintf(buffer, 512, (spec + L"ls").c_str(), va_arg(arguments, const wchar_t *));
            break;
        case L'S':
            swprintf(buffer, 512, (spec + L"s").c_str(), va_arg(arguments, const char *));
            break;
        default:
            buffer[0] = 0;
            break;
        }
        result += buffer;
        if (*character == 0) {
            break;
        }
    }
    va_end(arguments);
    wcscpy(output, result.c_str());
    return int(result.size());
}

wchar_t *lstrcpyW(wchar_t *target, const wchar_t *source) {
    return wcscpy(target, source);
}

wchar_t *lstrcpynW(wchar_t *target, const wchar_t *source, int size) {
    if (size <= 0) {
        return target;
    }
    wcsncpy(target, source, size - 1);
    target[size - 1] = 0;
    return target;
}

wchar_t *lstrcatW(wchar_t *target, const wchar_t *source) {
    return wcscat(target, source);
}

int lstrlenW(const wchar_t *text) {
    return int(wcslen(text));
}

int lstrcmpW(const wchar_t *first, const wchar_t *second) {
    return wcscmp(first, second);
}

int lstrcmpiW(const wchar_t *first, const wchar_t *second) {
    return wcscasecmp(first, second);
}

FILE *_wfopen(const wchar_t *path, const wchar_t *mode) {
    return fopen(narrow_path(path).c_str(), narrow_path(mode).c_str());
}
