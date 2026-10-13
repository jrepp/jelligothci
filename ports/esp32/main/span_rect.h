#ifndef JELLI_SPAN_RECT_H
#define JELLI_SPAN_RECT_H
#include "../../../core/pet_canvas.h"
void jelli_span_rect_mode(bool simd);
void jelli_canvas_rect_simd(Canvas *canvas, int x, int y, int w, int h, uint16_t color);
#endif
