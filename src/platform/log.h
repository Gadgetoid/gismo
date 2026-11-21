#pragma once

namespace gismo {

void log_open(const wchar_t *path);
void log_line(const wchar_t *format, ...);
void log_close();

}
