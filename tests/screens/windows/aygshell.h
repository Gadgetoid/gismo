#pragma once

#include <windows.h>

BOOL SHFullScreen(HWND window, DWORD state);
BOOL AllKeys(BOOL all);

#define SHFS_HIDETASKBAR 0x2
#define SHFS_HIDESIPBUTTON 0x8
#define SHFS_HIDESTARTICON 0x20
