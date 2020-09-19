
#pragma once

#include "draw.h"
#include "polygon.h"

enum {
	glyph_height = 8,
	glyph_width  = 5,
};

/** Выводит строку символов с центровкой относительно заданных координат */
void draw_text(const char *str, const struct polygon *poly, struct vec4 at,
               fn_painter, struct color color, struct draw_ctx *restrict ctx);

void text_lines(const char *const text[], int lines, const struct polygon *poly, struct vec4 at,
               fn_painter, struct color color, struct draw_ctx *restrict ctx);
