#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

namespace gismo {

void exe_folder(wchar_t *folder) {
    GetModuleFileNameW(nullptr, folder, MAX_PATH);
    wchar_t *slash = wcsrchr(folder, L'\\');
    if (slash) {
        *slash = 0;
    }
}

void utf8_to_wide(const char *text, wchar_t *output, int output_size) {
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, output, output_size) == 0) {
        output[0] = 0;
    }
}

bool folder_exists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != 0xFFFFFFFF && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void data_root(wchar_t *folder) {
    find_data_folder(L"Maps", folder);
    wchar_t *slash = wcsrchr(folder, L'\\');
    if (folder_exists(folder) && slash) {
        *slash = 0;
    } else {
        exe_folder(folder);
    }
}

void find_data_folder(const wchar_t *name, wchar_t *folder) {
    wchar_t base[MAX_PATH];
    exe_folder(base);
    wsprintfW(folder, L"%s\\%s", base, name);
    if (folder_exists(folder)) {
        return;
    }
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(L"\\*", &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (found.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY)) {
            wchar_t candidate[MAX_PATH];
            wsprintfW(candidate, L"\\%s\\GISmo\\%s", found.cFileName, name);
            if (folder_exists(candidate)) {
                lstrcpyW(folder, candidate);
                break;
            }
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

int read_file(const wchar_t *path, char *buffer, int size) {
    FILE *file = _wfopen(path, L"rb");
    if (file == nullptr) {
        return -1;
    }
    int length = int(fread(buffer, 1, size, file));
    fclose(file);
    return length;
}

void load_settings() {
    wchar_t path[MAX_PATH];
    exe_folder(path);
    lstrcatW(path, L"\\GISmo.ini");
    static char text[4096];
    int length = read_file(path, text, sizeof(text));
    if (length < 0) {
        log_line(L"settings: defaults");
        return;
    }
    log_line(L"settings: %d applied from GISmo.ini", apply_settings(text, length, app.settings));
}

bool write_whole_file(const wchar_t *path, const void *data, DWORD length) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, data, length, &written, nullptr) && written == length;
    CloseHandle(file);
    return ok;
}

}
