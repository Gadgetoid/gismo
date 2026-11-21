#pragma once

#include <windows.h>

typedef unsigned long ULONG;

#define D0 0
#define POWER_NAME 1

HANDLE SetPowerRequirement(LPCWSTR device, int state, ULONG flags, void *system_state, ULONG system_flags);
DWORD ReleasePowerRequirement(HANDLE handle);
