#include "font.h"
#include "font8x8.h" // Подключаем файл с массивом 8x8
#include "gfx.h"

void drawchar(int x, int y, char c, uint32_t color) {
    int index;
    
    // Проверка, что символ входит в стандартный ASCII (от пробела до '~')
    if (c >= 0x20 && c <= 0x7E)
        index = c - 0x20; // Убрал +1 (см. важное примечание ниже)
    else
        index = 0; // Индекс для неизвестного символа (можно сделать пробел или знак вопроса)

    // Используем массив шрифта 8x8 (убедись, что имя массива совпадает с тем, что в .h файле)
    const uint8_t *glyph = font_8x8[index]; 

    for (int row = 0; row < 8; row++) { // Изменили лимит с 16 на 8
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                putpixel(x + col, y + row, color);
            }
        }
    }
}

void drawstring(int x, int y, const char *s, uint32_t color) {
    int cx = x;
    while (*s) {
        drawchar(cx, y, *s, color);
        cx += 8; // Ширина символа остается 8, шаг не меняем
        s++;
    }
}