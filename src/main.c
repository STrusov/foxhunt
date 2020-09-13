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
#include <limits.h>
#include <time.h>

#include "ay_music.h"
#include "vulkan.h"
#include "wayland_window.h"

#include "polygon.h"
#include "text.h"

#include "triangulatio.h"

const float aspect_ratio = 4.0/3.0;
//const float aspect_ratio = 16.0/9.0;

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

static struct timespec start_time;

static bool game_started;

static char move[] = { '0', '0', '\x00' };

static char foxc[] = { '0', '/', '0', '\x00' };

enum {
	board_size_max = 9,
};

static int board_size = 9;

static int board_cell_x = -1;
static int board_cell_y = -1;

struct board_cell {
	int	fox_here;
	int	fox_visible;
	int	open;
};

static struct board_cell board[board_size_max*board_size_max];

static struct board_cell* board_at(int x, int y)
{
	return &board[x + y * board_size];
}

static bool board_over(float x, float y, uint32_t button, uint32_t state)
{
	// Делитель здесь и в вычислениях x100 y100 после скобок — размер стороны
	// игрового поля, равный меньшей стороне окна. Соответствует aspect_ratio.
	if (x >= 2.0f / aspect_ratio - 1.0f)
		return false;

	int x100 = 100 * (x + 1.0f) / aspect_ratio * (board_size - 1);
	int y100 = 100 * (y + 1.0f/aspect_ratio ) / aspect_ratio * (board_size - 1);
	if (x100 % 100 > 4 && x100 % 100 < 97)
		board_cell_x = x100 / 100;
	if (y100 % 100 > 4 && y100 % 100 < 97)
		board_cell_y = y100 / 100;

	if (button && state && board_cell_x >= 0 && board_cell_y >= 0) {
		struct board_cell *cell = board_at(board_cell_x, board_cell_y);
		if (cell->open < INT_MAX)
			++cell->open;
	}
	return true;
}

static void board_draw(int stage, struct draw_ctx *restrict ctx)
{
	const float step = 2.0f;
	float y = 1.0f - board_size;
	for (int yc = 0; yc < board_size; ++yc, y += step) {
		float x = 1.0f - board_size * aspect_ratio;
		for (int xc = 0; xc < board_size; ++xc, x += step) {
			struct vec4 at = {
				.x = x,
				.y = y,
				.z = 0,
				.w = board_size * aspect_ratio,
			};
			bool hover = xc == board_cell_x && yc == board_cell_y;
			const struct color cc = hover ? (struct color){ 0.5f, 0.5f, 0.5f, 0.5f }
			                              : (struct color){ 0.5f, 0.4f, 0.1f, 0.5f };
			poly_draw(&square094, at, NULL, cc, stage, ctx);
			const struct board_cell *cell = board_at(xc, yc);
			if (cell->open > 0) {
				char num[2] = { cell->fox_visible + '0', '\x00' };
				draw_text(num, &polygon8, at, NULL, (struct color){ 0.95f, 0.2f, 0.2f, 0.95f },
				          stage, ctx);
			}
		}
	}
}

static void rectangle(int stage, struct draw_ctx *restrict ctx,
                      struct vec4 at, float hw, float hh, struct color color)
{
	if (stage) {
		ctx->vert_buf[0].pos = (struct vec4){ at.x - hw, at.y + hh, at.z, at.w };
		ctx->vert_buf[0].color = color;
		ctx->vert_buf[1].pos = (struct vec4){ at.x - hw, at.y - hh, at.z, at.w };
		ctx->vert_buf[1].color = color;
		ctx->vert_buf[2].pos = (struct vec4){ at.x + hw, at.y - hh, at.z, at.w };
		ctx->vert_buf[2].color = color;
		ctx->vert_buf[3].pos = (struct vec4){ at.x + hw, at.y + hh, at.z, at.w };
		ctx->vert_buf[3].color = color;
		ctx->indx_buf[0] = ctx->base;
		ctx->indx_buf[1] = ctx->base + 1;
		ctx->indx_buf[2] = ctx->base + 2;
		ctx->indx_buf[3] = ctx->base + 2;
		ctx->indx_buf[4] = ctx->base + 3;
		ctx->indx_buf[5] = ctx->base;
		ctx->base += 4;
	}
	ctx->vert_buf += 4;
	ctx->indx_buf += 6;
}

static void title(int stage, struct draw_ctx *restrict ctx, struct vec4 at)
{
	rectangle(stage, ctx, at, 4.5f, 2.5f, (struct color){ 0.05f, 0.05f, 0.05f, 0.8f });
	static const char *text[] = {
		"ОХОТА",
		"НА ЛИС",
	};
	text_lines(text, sizeof(text)/sizeof(*text), &polygon8, at,
	           NULL, (struct color){ 0.0, 0.9, 0.0, 0.9 }, stage, ctx);
}

struct timespec time_from_start(void)
{
	struct timespec now;
	int r = timespec_get(&now, TIME_UTC);
	assert(r == TIME_UTC);
	const bool carry = now.tv_nsec < start_time.tv_nsec;
	return (struct timespec) {
		.tv_sec  = now.tv_sec  - start_time.tv_sec - carry,
		.tv_nsec = now.tv_nsec - start_time.tv_nsec + (carry ? 1000000000 : 0),
	};
}

static void score(int stage, struct draw_ctx *restrict ctx, struct vec4 at)
{
	rectangle(stage, ctx, at, 4.3f, 7.5f, (struct color){ 0.05f, 0.05f, 0.05f, 0.8f });

	// Строку со временем формируем на первом этапе.
	static char playtime[6];
	if (!stage) {
		const struct timespec pt = time_from_start();
		time_t m = pt.tv_sec / 60;
		long   s = pt.tv_sec % 60;
		if (m > 99 || m < 0)
			m = s = 99;
		playtime[0] = '0' + m / 10;
		playtime[1] = '0' + m % 10;
		playtime[2] = pt.tv_nsec < 1000000000/2 ? ':' : '\x01';
		playtime[3] = '0' + s / 10;
		playtime[4] = '0' + s % 10;
		playtime[5] = '\x0';
	}

	static const char *text[][2] = {
		{ "ВРЕМЯ", playtime },
		{ "ХОДЫ",  move },
		{ "ЛИСЫ",  foxc },
	};
	const int pairs = sizeof(text)/sizeof(*text);
	for (int s = 0; s < pairs; ++s) {
		const float dy = 5.0f * (s - 0.5f * (pairs-1));
		text_lines(text[s], 2, &polygon8, (struct vec4){ at.x, at.y + dy, at.z, at.w },
		           NULL, (struct color){ 0.0, 0.9, 0.0, 0.9 }, stage, ctx);
	}
}

struct area {
	float	left;
	float	right;
	float	top;
	float	bottom;
};

static inline
void area_set(struct area *aa, struct vec4 at, float hw, float hh)
{
	aa->left   = (at.x - hw) / at.w;
	aa->right  = (at.x + hw) / at.w;
	aa->top    = (at.y - hh) / at.w;
	aa->bottom = (at.y + hh) / at.w;
}

static inline bool area_over(struct area *aa, float x, float y)
{
	return aa->left < x && x < aa->right && aa->top < y && y < aa->bottom;
}

static struct area button_start, button_exit;
bool button_start_over, button_exit_over;

static void menu(int stage, struct draw_ctx *restrict ctx, struct vec4 at)
{
	rectangle(stage, ctx, at, 4.5f, 2.8f, (struct color){ 0.05f, 0.05f, 0.05f, 0.8f });
	const float dy = 1.5f;

	area_set(&button_start, (struct vec4){ at.x, at.y - dy, at.z, at.w }, 4.3f, 1.1f);
	if (button_start_over)
		rectangle(stage, ctx, (struct vec4){ at.x, at.y - dy, at.z, at.w }, 4.3f, 1.1f, (struct color){ 0.5f, 0.5f, 0.5f, 0.8f });
	draw_text(game_started ? "СТОП":"СТАРТ", &polygon8, (struct vec4){ at.x, at.y - dy, at.z, at.w },
	          NULL, (struct color){ 0.0, 0.9, 0.0, 0.9 }, stage, ctx);

	area_set(&button_exit, (struct vec4){ at.x, at.y + dy, at.z, at.w }, 4.3f, 1.1f);
	if (button_exit_over)
		rectangle(stage, ctx, (struct vec4){ at.x, at.y + dy, at.z, at.w }, 4.3f, 1.1f, (struct color){ 0.5f, 0.5f, 0.5f, 0.8f });
	draw_text("ВЫХОД", &polygon8, (struct vec4){ at.x, at.y + dy, at.z, at.w },
	          NULL, (struct color){ 0.9, 0.0, 0.0, 0.9 }, stage, ctx);
}

static void start_game(void)
{
	timespec_get(&start_time, TIME_UTC);
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
		vert_index *indx_buf = NULL;
		if (stage)
			r = vk_begin_index_buffer(vk, total_indices * sizeof(vert_index), &indx_buf);
		indx = (struct tri_index*)indx_buf;
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

		board_draw(stage, &dc);

		const float tw = 18.5f;
		const float twa = tw / aspect_ratio;
		title(stage, &dc, (struct vec4){ twa, -0.7f * twa, 0.0f, tw });

		const float sw = 15.0f * aspect_ratio;
		score(stage, &dc, (struct vec4){ sw/aspect_ratio, 0.05f * sw, 0.0f, sw });

		const float mw = 21.0f * aspect_ratio;
		const float mwa = mw / aspect_ratio;
		menu(stage, &dc, (struct vec4){ mwa, 0.8f * mwa, 0.0f, mw });

		if (stage) {
			assert(total_vertices == dc.vert_buf - vert_buf);
			assert(total_indices  == dc.indx_buf - indx_buf);
		}
		total_vertices = dc.vert_buf - vert_buf;
		total_indices  = dc.indx_buf - indx_buf;
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
		vk_cmd_draw_indexed(vk, total_indices);
	r = vk_end_render_cmd(vk);

	r = vk_present_frame(vk);
	return (r == VK_SUCCESS);
}

static bool pointer_click(const struct window *window, double x, double y,
                          const char **cursor_name, uint32_t button, uint32_t state)
{
	button_start_over = false;
	button_exit_over  = false;
	board_cell_x = -1;
	board_cell_y = -1;
	if (x >= 0) {
		float xh = (2.0 * x / window->width) - 1.0;
		float yh = ((2.0 * y / window->height) - 1.0) / window->aspect_ratio;

		if (board_over(xh, yh, button, state)) {
			goto over;
		}
		if (area_over(&button_start, xh, yh)) {
			button_start_over = true;
			goto over;
		}
		if (area_over(&button_exit, xh, yh)) {
			button_exit_over = true;
			goto over;
		}
	}
	return false;
over:
	*cursor_name = "left_ptr";
	return true;
}

static
void pointer_over(const struct window *window, double x, double y, const char **cursor_name)
{
	pointer_click(window, x, y, cursor_name, 0, 0);
}

static const struct render vulkan = {
	.create    	= vk_window_create,
	.destroy   	= vk_window_destroy,
	.draw_frame	= draw_frame,
	.resize    	= vk_window_resize,
};

static const struct controller controller = {
	.hover  	= pointer_over,
	.click  	= pointer_click,
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

	start_game();

	struct window window = {
		.ctrl 	= &controller,
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

