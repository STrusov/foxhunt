/**\file
 * \brief	Интерфейс для вывода многогранников.
 */

#pragma once

#include "draw.h"

/** Хранит индексы вершин треугольника. */
struct tri_index {
	vert_index v[3];
};

/** Определяет многогранник. */
struct polygon {
	/** Вершины формирующих данную фигуру треугольников. */
	struct pos2d    	*vertex;
	/** Соответствующие вершинам треугольников индексы. */
	struct tri_index	*index;
	/** Количество вершин треугольников. */
	fast_index      	vert_count;
	/** Количество треугольников (индексов больше втрое) */
	fast_index      	tri_count;
};

const struct polygon square094;
const struct polygon square108;
const struct polygon polygon8;

/** Инициализирует полигоны. */
void poly_init(void);

/** Используется для окрашивания вершин.
 * \param color	базовый цвет.
 */
typedef void fn_painter(struct vertex*, struct color, unsigned);

/** Выводит многогранник
 * \param p         	описывает фигуру
 * \param coordinate	задаёт центр
 * \param painter   	изменяет цвет вершин, либо NULL для раскрашивания в базовый цвет
 * \param color     	базовый цвет вершин
 */
void poly_draw(const struct polygon *p, struct vec4 coordinate,
               fn_painter painter, struct color color, struct draw_ctx *restrict ctx);
