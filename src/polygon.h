
#pragma once

#include "draw.h"

struct tri_index {
	vert_index v[3];
};

struct polygon {
	struct pos2d    	*vertex;
	struct tri_index	*index;
	fast_index      	vert_count;
	/** Количество треугольников (индексов больше втрое) */
	fast_index      	tri_count;
};

const struct polygon square094;
const struct polygon square108;
const struct polygon polygon8;

void poly_init(void);

typedef void fn_painter(struct vertex*, struct color, unsigned);

/** Параметр \stage определяет производится ли отрисовка, или определяются размеры буферов. */
void poly_draw(const struct polygon *p, struct vec4 coordinate,
               fn_painter, struct color color, struct draw_ctx *restrict ctx);
