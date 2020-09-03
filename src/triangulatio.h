
#include <assert.h>
#ifndef	NDEBUG
#include <stdio.h>
#endif

struct triangle {
	fast_index vertex[3];
	fast_index neighbor[3];
};

static const float d_eps = 0;

static inline
float dc_sa(float x, float y, float x1, float y1, float x3, float y3)
{
	return (x - x1)*(x - x3) + (y - y1)*(y - y3);
}

static inline
float dc_sb(float x1, float y1, float x2, float y2, float x3, float y3)
{
	return (x2 - x1)*(x2 - x3) + (y2 - y1)*(y2 - y3);
}

static inline
bool delaunay(float x, float y, float x1, float y1, float x2, float y2, float x3, float y3)
{
	float d = ((x - x1)*(y - y3) - (x - x3)*(y - y1)) * dc_sb(x1, y1, x2, y2, x3, y3)
	        + dc_sa(x, y, x1, y1, x3, y3) * ((x2 - x1)*(y2 - y3) - (x2 - x3)*(y2 - y1));
	return d >= -d_eps;
}

/**
 * Строит по набору вершин множество треугольников (триангуляцию).
 * Работает на ограниченном множестве входных данных (упорядоченные "веером"
 * вокруг центральной точки; без центра порождает лишние невидимые треугольники).
 */
static
void triangulate(struct vertex *restrict vert, unsigned const vcnt,
                 struct tri_index *restrict *indices, unsigned *restrict tcnt)
{
	struct tri_index *triangle = *indices;
	unsigned idx = 0;
#ifndef	NDEBUG
	unsigned iter = 0, conv = 0;
#endif
	// Первые три вершины объединяем в треугольник.
	triangle->v[0] = idx++;
	triangle->v[1] = idx++;
	triangle->v[2] = idx++;
	++triangle;
	// Последующие точки связываем с ближайшей стороной предыдущего треугольника,
	// если построение не нарушает условие Делоне. Иначе перестраиваем треугольники.
	// Последний треугольник аналогично связываем с первым.
	for (; vcnt > 3 && idx <= vcnt; ++idx) {
#ifndef	NDEBUG
		++iter;
#endif
		assert((vert_index)idx == idx);
		if (triangle == &(*indices)[*tcnt]) {
			// TODO увеличить буфер
			assert(0);
		}
		struct tri_index *pt = triangle - 1;
		// Проверяем сумму противолежащих углов.
		// см. А.В. Скворцов "Триангуляция Делоне и её применение"
		//    v[0] (x1,y1) +-------+ (x2,y2) v[1]
		//                 +\     β+
		//                 + \     +
		//                 +  \    +
		//                 +   \   +
		//                 +    \  +
		//                 +     \ +
		//                 +α     \+
		// vert[idx] (x,y) +-------+ (x3,y3) v[2]
		//          Рис. 15
		// Если sα ≥ 0 и sβ ≥ 0, то α ≤ 90, β ≤ 90 и условие Делоне выполняется.
		// Если sα < 0 и sβ < 0, то α > 90, β > 90 и условие Делоне не выполняется.
		// Иначе требуются полные вычисления.
		float x  = vert[idx].pos.x, y = vert[idx].pos.y;
		float x1 = vert[pt->v[0]].pos.x, y1 = vert[pt->v[0]].pos.y;
		float x2 = vert[pt->v[1]].pos.x, y2 = vert[pt->v[1]].pos.y;
		float x3 = vert[pt->v[2]].pos.x, y3 = vert[pt->v[2]].pos.y;
		float sa = dc_sa(x, y, x1, y1, x3, y3);
		float sb = dc_sb(x1, y1, x2, y2, x3, y3);
		bool dc;
		if (sa >= -d_eps && sb >= -d_eps) {
			dc = true;
		} else if (sa < 0 && sb < 0) {
			dc = false;
		} else {
			dc = delaunay(x, y, x1, y1, x2, y2, x3, y3);
		}
		if (dc) {
			// TODO VkPipelineRasterizationStateCreateInfo задаёт
			// VK_FRONT_FACE_CLOCKWISE и VK_CULL_MODE_BACK_BIT.
			// Индексы должны указывать расположенные по часовой стрелке вершины.
last:
			triangle->v[0] = pt->v[0];
			triangle->v[1] = pt->v[2];
			if (idx < vcnt) {
				triangle->v[2] = idx;
			} else {
				triangle->v[2] = (*indices)[0].v[1];
			}
			++triangle;
		} else {
			if (idx < vcnt) {
#if 1
				triangle->v[0] = pt->v[0];
				triangle->v[1] = pt->v[1];
				triangle->v[2] = idx;
				pt->v[0] = idx;
#else
				triangle->v[0] = pt->v[1];
				triangle->v[1] = pt->v[2];
				triangle->v[2] = idx;
				pt->v[2] = idx;
#endif
				++triangle;
#ifndef	NDEBUG
				++conv;
#endif
			} else {
				goto last;
			}
		}
	}
	*tcnt = triangle - *indices;
#ifndef	NDEBUG
	printf("Триангуляция: вершины (%u), треугольники (%u), итерации (%u), преобразования (%u)\n",
	       vcnt, *tcnt, iter, conv);
	for (int i = 0; i < *tcnt; ++i) {
		unsigned i0 = (*indices)[i].v[0];
		unsigned i1 = (*indices)[i].v[1];
		unsigned i2 = (*indices)[i].v[2];
		printf("  {%u[%.2f;%.2f] %u[%.2f;%.2f] %u[%.2f;%.2f]}",
				i0, vert[i0].pos.x, vert[i0].pos.y,
				i1, vert[i1].pos.x, vert[i1].pos.y,
				i2, vert[i2].pos.x, vert[i2].pos.y);
	}
	printf("\n");
#endif
}
