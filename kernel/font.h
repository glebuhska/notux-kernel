#ifndef FONT_H
#define FONT_H
#include "types.h"

void drawchar(int x, int y, char c, uint32_t color);
void drawstring(int x, int y, const char *s, uint32_t color);

#endif