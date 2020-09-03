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

#include "polygon.h"
#include "text.h"

#include "triangulatio.h"

const float aspect_ratio = 4.0/3.0;

/*
 Изображение строится по однородным (гомогенным) координатам вершин (clip coordinate),
 представляющим собой 4-х компонентный вектор (x, y, z, w), проецированием
 в нормализованные координаты устройства, ограниченные квадратом с координатами
 углов (±1, ±1). Сторона квадрата равна 2 (отсюда 2 в вычислениях и шаг циклах).

 Проекция выполняется делением пространственных координат (x, y, z) на 4-ю
 компоненту (w). Так масштабирование перекладывается на граф.процессор.

    Видимые координаты для окна с соотношением сторон 4:3
        (-w, -w) +----------------+ (w, -w)
                 |////////////////|
    (-w, -0.75w) |                | (w, -0.75w)
                 |                |
                 |        .       |
                 |      (0,0)     |
                 |                |
     (-w, 0.75w) |                | (w, 0.75w)
                 |////////////////|
         (-w, w) +----------------+ (w, w)

*/

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
		const int dot_cnt = 160;

		struct vertex *vert_buf = NULL;
		if (stage)
			r = vk_begin_vertex_buffer(vk, total_vertices * sizeof(struct vertex), &vert_buf);

		struct draw_ctx dc = {
			.vert_buf = vert_buf,
		};

		if (stage) {
			dc.vert_buf->pos.x = 0;
			dc.vert_buf->pos.y = 0;
			dc.vert_buf->pos.z = 0;
			dc.vert_buf->pos.w = 1.0f;
			dc.vert_buf->color.r = 0.5f;
			dc.vert_buf->color.g = 0.5f;
			dc.vert_buf->color.b = 0.5f;
			dc.vert_buf->color.a = 1.0f;
		}
		++dc.vert_buf;
		for (unsigned i = 0; i < cnt; ++i) {
			if (stage) {
				dc.vert_buf->pos.x = 0.95f * cosf(angle + i * 2*PI/cnt);
				dc.vert_buf->pos.y = 0.95f * sinf(angle + i * 2*PI/cnt);
				dc.vert_buf->pos.z = 0;
				dc.vert_buf->pos.w = 2.0f;
				dc.vert_buf->color.r = 0.3f + 0.7f * cosf(2 * angle + i * 2*PI/(4*cnt));
				dc.vert_buf->color.g = 0.3f + 0.7f * cosf(3 * angle + i * 2*PI/(3*cnt));
				dc.vert_buf->color.b = 0.3f + 0.7f * cosf(4 * angle + i * 2*PI/(2*cnt));
				dc.vert_buf->color.a = 0.0f;
			}
			++dc.vert_buf;
		}
		const unsigned vcnt = dc.vert_buf - vert_buf;

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

		dc.indx_buf = &indx[tcnt].v[0];
		dc.base = vcnt;
		for (int y = -dot_cnt/aspect_ratio + 1; y < dot_cnt/aspect_ratio; y += 2) {
			for (int x = -dot_cnt + 1; x < dot_cnt; x += 2) {
				poly_draw(&polygon8, (struct vec4){ x, y, 0, dot_cnt },
				          NULL, (struct color){ 0.5, 0.5, 0.5, 0.1 },
				          stage, &dc);
			}
		}
		const char *text[] = {
			"ОХОТА НА ЛИС",
			"! \"#$%&'",
			"()*+,-./",
			"0123456789",
			"АБВГДЕЖЗИЙ",
			"КЛМНОПРСТУФХ",
			"ЦЧШЩЪЫЬЭЮЯ",
		};
		const int text_lines = sizeof(text)/sizeof(*text);
		for (int s = 0; s < text_lines; ++s) {
			const struct vec4 pos = {
				.x = 0,
				.y = 2 * (glyph_height + 1) * (s + 0.5f * (1 - text_lines)),
				.z = 0,
				.w = 6,
			};
			draw_text(text[s], &polygon8, pos, NULL, (struct color){ 0.0, 0.9, 0.0, 0.9 },
			          stage, &dc);
		}

		if (stage) {
			assert(total_vertices == dc.vert_buf - vert_buf);
			assert(total_indices  == dc.indx_buf - (vert_index*)indx_buf);
		}
		total_vertices = dc.vert_buf - vert_buf;
		total_indices  = dc.indx_buf - (vert_index*)indx_buf;
	}
	vk_end_vertex_buffer(vk);
	vk_end_index_buffer(vk);

	r = vk_begin_render_cmd(vk);
		// TODO Достаточно установить однократно.
		const struct transform transform = {
			.scale     = { 1.0, aspect_ratio, 1.0, 1.0 },
			.translate = { 0 },
		};
		vk_cmd_push_transform(vk, &transform);
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

	poly_init();

	struct window window = {
		.render	= &vulkan,
		.title	= "Окно",
		.width	= 800,
//		.height	= 600,
		.border = 10,
		.aspect_ratio = aspect_ratio,
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

