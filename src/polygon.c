
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "polygon.h"

const struct polygon square094 = {
	.vertex     = (struct pos2d     [5]) {},
	.index      = (struct tri_index [4]) {},
	.vert_count = 5,
	.tri_count  = 4,
};

const struct polygon square108 = {
	.vertex     = (struct pos2d     [5]) {},
	.index      = (struct tri_index [4]) {},
	.vert_count = 5,
	.tri_count  = 4,
};

const struct polygon octagon150 = {
	.vertex     = (struct pos2d     [9]) {},
	.index      = (struct tri_index [8]) {},
	.vert_count = 9,
	.tri_count  = 8,
};

/** Заполняет массив индексов, строя триангуляцию для заданных вершин. */
static void poly_triangulate(const struct polygon *p)
{
	struct tri_index *triangle = p->index;
	unsigned idx = 0;
#ifndef	NDEBUG
	unsigned iter = 0, conv = 0;
#endif
	// Первые три вершины объединяем в треугольник.
	triangle->v[0] = idx++;
	triangle->v[1] = idx++;
	triangle->v[2] = idx++;
	++triangle;
	// Последующие точки связываем с ближайшей стороной предыдущего треугольника.
	for (; p->vert_count > 3 && idx <= p->vert_count; ++idx) {
#ifndef	NDEBUG
		++iter;
#endif
		assert((vert_index)idx == idx);
		assert(triangle != &p->index[p->tri_count]);
		struct tri_index *pt = triangle - 1;
		// VkPipelineRasterizationStateCreateInfo задаёт
		// VK_FRONT_FACE_CLOCKWISE и VK_CULL_MODE_BACK_BIT.
		// Индексы должны указывать расположенные по часовой стрелке вершины.
		triangle->v[0] = pt->v[0];
		triangle->v[1] = pt->v[2];
		triangle->v[2] = idx < p->vert_count ? idx : p->index[0].v[1];
		++triangle;
	}
	assert(p->tri_count == triangle - p->index);
#ifndef	NDEBUG
	printf("Триангуляция: вершины (%u), треугольники (%u), итерации (%u), преобразования (%u)\n",
			p->vert_count, p->tri_count, iter, conv);
	for (int i = 0; i < p->tri_count; ++i) {
		unsigned i0 = p->index[i].v[0];
		unsigned i1 = p->index[i].v[1];
		unsigned i2 = p->index[i].v[2];
		printf("  {%u[%.2f;%.2f] %u[%.2f;%.2f] %u[%.2f;%.2f]}",
				i0, p->vertex[i0].x, p->vertex[i0].y,
				i1, p->vertex[i1].x, p->vertex[i1].y,
				i2, p->vertex[i2].x, p->vertex[i2].y);
	}
	printf("\n");
#endif
}

static void dot_triangulate(const struct polygon *p, float scale)
{
	p->vertex[0].x = 0;
	p->vertex[0].y = 0;
	const unsigned ccnt = p->vert_count - 1;
	for (unsigned i = 1; i <= ccnt; ++i) {
		p->vertex[i].x = scale * cosf((2*i + 1) * PI/ccnt);
		p->vertex[i].y = scale * sinf((2*i + 1) * PI/ccnt);
	}
	poly_triangulate(p);
}

void poly_init()
{
	dot_triangulate(&square094, 0.94f * 1.414213562f); // √2
	dot_triangulate(&square108, 1.08f * 1.414213562f); // √2
	dot_triangulate(&octagon150, 1.5);
}

static inline void color_copy(struct vertex *restrict vert, struct color src, unsigned i)
{
	vert->color = src;
}

void poly_draw(const struct polygon *p, struct vec4 coordinate, fn_painter painter,
               struct color color, struct draw_ctx *restrict ctx)
{
	if (!ctx->stage) {
		ctx->vert_buf += p->vert_count;
		ctx->indx_buf += 3 * p->tri_count;
		return;
	}
	if (!painter)
		painter = color_copy;
	for (unsigned i = 0; i < p->vert_count; ++i) {
		ctx->vert_buf->pos.x = coordinate.x + p->vertex[i].x;
		ctx->vert_buf->pos.y = coordinate.y + p->vertex[i].y;
		ctx->vert_buf->pos.z = coordinate.z;
		ctx->vert_buf->pos.w = coordinate.w;
		painter(ctx->vert_buf, color, i);
		++ctx->vert_buf;
	}
	for (unsigned i = 0; i < p->tri_count; ++i) {
		*ctx->indx_buf++ = p->index[i].v[0] + ctx->base;
		*ctx->indx_buf++ = p->index[i].v[1] + ctx->base;
		*ctx->indx_buf++ = p->index[i].v[2] + ctx->base;
	}
	ctx->base += p->vert_count;
	assert(ctx->indx_buf[-1] < ctx->base);
}
