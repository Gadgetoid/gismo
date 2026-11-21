#pragma once

#include <windows.h>

void shim_set_tick(DWORD value);
void shim_set_card_folder(const char *folder);
const uint16_t *shim_bitmap_bits(HBITMAP bitmap);
