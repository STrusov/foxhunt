/**\file
 * \brief	Графические примитивы.
 */

#pragma once

#include <stdint.h>

#define PI 3.14159f

/** Размер типа индекса задаётся при инициализации Vulkan. */
typedef uint32_t vert_index;
typedef unsigned fast_index;

/** Двумерная координата */
struct pos2d {
	float	x;
	float	y;
};

/** Пространственная координата. */
struct vec4 {
	float	x;
	float	y;
	float	z;
	/** Гомогенная координата, используется для масштабирования. */
	float	w;
};

/** Цветовые компоненты с альфа-каналом. */
struct color {
	float	r;
	float	g;
	float	b;
	float	a;
};

/** Описатель вершины (точки в пространстве). */
struct vertex {
	struct vec4 	pos;
	struct color	color;
};

/** */
struct draw_ctx {
	/** Текущая позиция в буфере вершин. */
	struct vertex 	*restrict vert_buf;
	/** Текущая позиция в буфере индексов. */
	vert_index    	*restrict indx_buf;
	/** Текущий индекс, растёт при выводе индексов. */
	vert_index    	base;
	/** На 0-й стадии вместо отрисовки вычисляется размер буферов. */
	int           	stage;
};
