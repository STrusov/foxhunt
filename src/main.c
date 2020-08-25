/**
 * Пример клиента Wayland основан на следующих работах:
 *
 * Drew DeVault "The Wayland Protocol"
 * https://wayland-book.com
 *
 * Jan Newmarch "Programming Wayland Clients"
 * https://jan.newmarch.name/Wayland/
 *
 * Библиотека GLFW https://www.glfw.org
 * https://github.com/glfw/glfw/blob/master/README.md
 *
 *
 * Клиент Vulkan основан на https://vulkan-tutorial.com
 * https://github.com/Overv/VulkanTutorial/graphs/contributors
 *
 */

#include "ay_music.h"
#include "vulkan.h"
#include "wayland_window.h"

typedef uint32_t vert_index;
typedef unsigned fast_index;

#include "triangulatio.h"

const float PI = 3.14159f;

struct polygon {
	struct pos2d    	*vertex;
	struct tri_index	*index;
	fast_index      	vert_count;
	/** Количество треугольников (индексов больше втрое) */
	fast_index      	tri_count;
};

static const struct polygon polygon8 = {
	.vertex     = (struct pos2d     [9]) {},
	.index      = (struct tri_index [8]) {},
	.vert_count = 9,
	.tri_count  = 8,
};

enum {
	glyph_height = 8,
	glyph_width  = 5,
	glyph_count  = 1,
};

static const uint8_t font[glyph_count][glyph_height] = {
	{ 0x1e, 0x21, 0x21, 0x00, 0x21, 0x21, 0x1e, 0x00 },	// 0
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

static void gfx_init()
{
	dot_triangulate(&polygon8, 0.90);
}

static inline void color_copy(struct vertex2d *restrict vert, struct color src)
{
	vert->color = src;
}

/** Параметр \stage определяет производится ли отрисовка, или определяются размеры буферов. */
static
void poly_draw(const struct polygon *p, struct pos2d coord, float scale,
               void(painter)(struct vertex2d*, struct color), struct color color,
               int stage, struct vertex2d *restrict *restrict vert_buf,
               vert_index *restrict *restrict indx_buf, vert_index *base)
{
	if (!stage) {
		*vert_buf += p->vert_count;
		*indx_buf += 3 * p->tri_count;
		return;
	}
	if (!painter)
		painter = color_copy;
	for (unsigned i = 0; i < p->vert_count; ++i) {
		(*vert_buf)->pos.x = scale * p->vertex[i].x + coord.x;
		(*vert_buf)->pos.y = scale * p->vertex[i].y + coord.y;
		painter(*vert_buf, color);
		++*vert_buf;
	}
	for (unsigned i = 0; i < p->tri_count; ++i) {
		*(*indx_buf)++ = p->index[i].v[0] + *base;
		*(*indx_buf)++ = p->index[i].v[1] + *base;
		*(*indx_buf)++ = p->index[i].v[2] + *base;
	}
	*base += p->vert_count;
	assert((*indx_buf)[-1] < *base);
}

/** Вычисляет ширину глифа символа в "пикселях" */
static inline unsigned glyphwidth(unsigned idx)
{
	static unsigned char width[glyph_count];
	if (!width[idx]) {
		unsigned char w = 0;
		// накладываем строки глифа на 1 линию.
		uint8_t line = 0;
		for (int l = 0; l < glyph_height; ++l)
			line |= font[idx][l];
		while (line) {
			line >>= 1;
			++w;
		}
		if (!w)
			w = glyph_width;
		width[idx] = w;
	}
	return width[idx];
}

/** Выводит строку символов с центровкой относительно заданных координат */
static
void draw_text(char *str, const struct polygon *poly, struct pos2d at, float scale,
               void(painter)(struct vertex2d*, struct color), struct color color,
               int stage, struct vertex2d *restrict *restrict vert_buf,
               vert_index *restrict *restrict indx_buf, vert_index *base)
{
	int cnt;
	// Предварительно подготавливаем индексы в массиве font и ширину глифов.
	unsigned glidx[32] = {};
	unsigned width[32];
	for (cnt = 0; *str; ++str, ++cnt) {
		glidx[cnt] = *str - '0';
	}
	assert(cnt);
	unsigned line_width = 0;
	for (int i = 0; i < cnt; ++i) {
		width[i] = glyphwidth(glidx[i]);
		line_width += width[i] + 1; // межсимвольный интервал.
	}
	line_width -= 1;
	const float y0 = -glyph_height / 2.0;
	float x0 = line_width / -2.0;
	for (int c = 0; c < cnt; ++c) {
		for (int l = 0; l < glyph_height; ++l) {
			unsigned line = font[glidx[c]][l];
			for (int v = width[c]; v ; --v) {
				if (line & 1) {
					const struct pos2d xy = {
						.x = at.x + (v + x0) * scale/glyph_height,
						.y = at.y + (l + y0) * scale/glyph_height,
					};
					poly_draw(poly, xy, scale/glyph_height/2, painter, color,
					          stage, vert_buf, indx_buf, base);
				}
				line >>= 1;
			}
		}
		x0 += width[c] + 1;
	}
}

static bool draw_frame(void *p)
{
	ay_music_continue(5);

	struct vk_context *vk = p;
	VkResult r = vk_acquire_frame(vk);

	static float angle;
	angle = angle + PI/192;

	// На стадии 0 вычисляем размер буферов, на следующей их заполняем.
	unsigned total_indices;
	unsigned total_vertices;
	for (int stage = 0; stage <= 1; ++stage) {

		const unsigned cnt = 6;
		const unsigned dot_cnt = 160;

		void *vert_buf = NULL;
		if (stage)
			r = vk_begin_vertex_buffer(vk, total_vertices * sizeof(struct vertex2d), &vert_buf);
		struct vertex2d *vert = vert_buf;

		if (stage) {
			vert->pos.x = 0;
			vert->pos.y = 0;
			vert->color.r = 0.5f;
			vert->color.g = 0.5f;
			vert->color.b = 0.5f;
			vert->color.a = 1.0f;
		}
		++vert;
		for (unsigned i = 0; i < cnt; ++i) {
			if (stage) {
				vert->pos.x = 0.95f * cosf(angle + i * 2*PI/cnt);
				vert->pos.y = 0.95f * sinf(angle + i * 2*PI/cnt);
				vert->color.r = 0.3f + 0.7f * cosf(2 * angle + i * 2*PI/(4*cnt));
				vert->color.g = 0.3f + 0.7f * cosf(3 * angle + i * 2*PI/(3*cnt));
				vert->color.b = 0.3f + 0.7f * cosf(4 * angle + i * 2*PI/(2*cnt));
				vert->color.a = 0.0f;
			}
			++vert;
		}
		const unsigned vcnt = vert - (struct vertex2d*)vert_buf;

		unsigned tcnt;
		struct tri_index *indx;
		void *indx_buf = NULL;
		if (stage)
			r = vk_begin_index_buffer(vk, total_indices * sizeof(vert_index), &indx_buf);
		indx = indx_buf;
		if (stage)
			triangulate(vert_buf, vcnt, &indx, &tcnt);
		else
			tcnt = vcnt - 1;

		vert_index current = vcnt;
		vert_index *cur_idx = &indx[tcnt].v[0];
		for (int y = 0; y < dot_cnt; ++y) {
			for (int x = 0; x < dot_cnt; ++x) {
				struct pos2d xy = {
					.x = ((2*x + 1) / (float)dot_cnt) - 1,
					.y = ((2*y + 1) / (float)dot_cnt) - 1,
				};
				poly_draw(&polygon8, xy, 1./dot_cnt,
				          NULL, (struct color){ 0.5, 0.5, 0.5, 0.1 },
				          stage, &vert, &cur_idx, &current);
			}
		}
		struct pos2d pos = {
			.x = 0,
			.y = 0,
		};
		draw_text("00000", &polygon8, pos, 1./8, NULL, (struct color){ 0.0, 0.9, 0.0, 0.9 },
				  stage, &vert, &cur_idx, &current);

		if (stage) {
#ifndef	NDEBUG
			printf("total_vertices = %u\ttotal_indices = %u\n", total_vertices, total_indices);
#endif
			assert(total_vertices == vert - (struct vertex2d*)vert_buf);
			assert(total_indices  == cur_idx - (vert_index*)indx_buf);
		}
		total_vertices = vert - (struct vertex2d*)vert_buf;
		total_indices  = cur_idx - (vert_index*)indx_buf;
	}
	vk_end_vertex_buffer(vk);
	vk_end_index_buffer(vk);

	r = vk_begin_render_cmd(vk);
		vk_cmd_draw_indexed(vk, total_indices, sizeof(vert_index));
	r = vk_end_render_cmd(vk);

	r = vk_present_frame(vk);
	return (r == VK_SUCCESS);
}

static const struct render vulkan = {
	.create    	= vk_window_create,
	.destroy   	= vk_window_destroy,
	.draw_frame	= draw_frame,
	.resize    	= vk_window_resize,
};

int main(int argc, char *argv[])
{
	ay_music_init();
	ay_music_play();

	if (!wayland_init())
		return 1;

	if (vk_init() != VK_SUCCESS)
		return 2;

	gfx_init();
//	return 0;

	struct window window = {
		.render	= &vulkan,
		.title	= "Окно",
		.width	= 640,
		.height	= 640,
		.border = 10,
		.aspect_ratio = 1,
//		.constant_aspect_ratio = true,
	};
	window_create(&window);
	window_dispatch(&window);

	window_destroy(&window);

	vk_stop();
	wayland_stop();
	ay_music_stop();

	printf("Выход.\n");
	return 0;
}

