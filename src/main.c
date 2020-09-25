/**
 * \mainpage	Охота на «лис»
 *
 * Программа основана на одноимённой публикации автора А. Несчетный
 * в №12 журнала «Наука и Жизнь» за 1985 г
 * и является переизданием [версии для ZX-Spectrum](https://zxart.ee/rus/soft/game/fox-hunt-ohota-na-lis/).
 *
 * Пример клиента Wayland основан на работах
 * [«The Wayland Protocol»](https://wayland-book.com) Drew DeVault,
 * [«Programming Wayland Clients»](https://jan.newmarch.name/Wayland/) Jan Newmarch и
 * библиотеке [GLFW](https://www.glfw.org) ([авторы](https://github.com/glfw/glfw/blob/master/README.md#acknowledgements)).
 *
 * Клиент Vulkan основан на
 * [Vulkan Tutorial](https://vulkan-tutorial.com) ([авторы](https://github.com/Overv/VulkanTutorial/graphs/contributors)).
 *
 * Эмулятор музыкального процессора AY-3-8912 основан на одной из ранних версий
 * [UnrealSpeccy](https://speccy.info/UnrealSpeccy) (автор [SMT](https://speccy.info/SMT)).
 *
 * Используются музыкальные композиции Столяренко Е.В. (S.J. Soft).
 *
 * Исходные тексты публикуются как достояние общественности (CC0) в той мере,
 * в которой это не нарушает права авторов заимствованных фрагментов
 * (UnrealSpeccy распространяется на условиях GPL, соответственно данное
 * составное произведение может подпадать под GPL).
 *
 * \author Трусов С.А. <sergei.a.trusov@ya.ru>
 *
 * \file
 * \brief Реализация игры «Охота на лис».
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <linux/input-event-codes.h>

#include "ay_music.h"
#include "vulkan.h"
#include "wayland_window.h"

#include "polygon.h"
#include "text.h"

#define APP_VERSION "0.12-альфа"

#ifndef BOARD_MAX_SIZE
#define BOARD_MAX_SIZE 9
#endif

#ifndef MAX_FOX_IN_CELL
#define MAX_FOX_IN_CELL 1
#endif

#define COLOR_CELL      	((struct color){ 0.00f, 0.00f, 0.10f, 0.85f })
#define COLOR_CELL_HOVER	((struct color){ 0.30f, 0.30f, 0.30f, 0.85f })
#define COLOR_CELL_TEXT 	((struct color){ 1.00f, 0.50f, 0.00f, 0.85f })
#define COLOR_CELL_FOX  	((struct color){ 0.95f, 0.20f, 0.20f, 0.95f })
#define COLOR_CELL_ANIM 	((struct color){ 0.20f, 0.20f, 0.20f, 0.95f })
#define COLOR_BOX       	((struct color){ 0.00f, 0.00f, 0.00f, 0.85f })
#define COLOR_BOX_A     	((struct color){ 0.00f, 0.00f, 0.00f, 0.70f })
#define COLOR_HOVER     	((struct color){ 0.50f, 0.50f, 0.50f, 0.80f })
#define COLOR_TITLE     	((struct color){ 0.00f, 0.90f, 0.00f, 0.90f })
#define COLOR_AUTHORS   	((struct color){ 0.80f, 0.80f, 0.80f, 0.90f })
#define COLOR_INTRO     	((struct color){ 0.00f, 0.90f, 0.00f, 0.90f })
#define COLOR_SCORE     	((struct color){ 0.00f, 0.90f, 0.00f, 0.90f })
#define COLOR_START     	((struct color){ 0.00f, 0.90f, 0.00f, 0.90f })
#define COLOR_STOP      	((struct color){ 0.90f, 0.90f, 0.00f, 0.90f })
#define COLOR_EXIT      	((struct color){ 0.90f, 0.00f, 0.00f, 0.90f })
#define COLOR_BACKGROUND	((struct color){ 0.10f, 0.10f, 0.10f, 0.25f })

const char game_name[] = "Охота на лис";

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
static int board_size = BOARD_MAX_SIZE;
static int fox_count = 5;
static int move_max = 99;
static int fox_found;
static int move;

/** TODO вычислять из частоты кадров. */
static int phase_per_sec = 60;

enum game_state {
	gs_intro,
	gs_play,
	gs_finish,
};
static enum game_state game_state = gs_intro;

static const struct polygon square094 = {
	.vertex     = (struct pos2d     [5]) {},
	.index      = (struct tri_index [4]) {},
	.vert_count = 5,
	.tri_count  = 4,
};
static const struct polygon square108 = {
	.vertex     = (struct pos2d     [5]) {},
	.index      = (struct tri_index [4]) {},
	.vert_count = 5,
	.tri_count  = 4,
};
static const struct polygon octagon150 = {
	.vertex     = (struct pos2d     [9]) {},
	.index      = (struct tri_index [8]) {},
	.vert_count = 9,
	.tri_count  = 8,
};

static int board_cell_x = -1;
static int board_cell_y = -1;

struct board_cell {
	int	fox;
	int	found;
	int	visible;
	int	open;
	int	animation;
};

static struct board_cell board[BOARD_MAX_SIZE*BOARD_MAX_SIZE];

static struct board_cell* board_at(int x, int y)
{
	return &board[x + y * board_size];
}

/** Расставляет лис на поле */
static void board_init(void)
{
	move = 0;
	fox_found = 0;
	for (int i = 0; i < sizeof(board)/sizeof(*board); ++i)
		board[i] = (struct board_cell) {};
	for (int fox = 0; fox < fox_count; /**/) {
		int x = rand() % board_size;
		int y = rand() % board_size;
		if (board_at(x, y)->fox < MAX_FOX_IN_CELL) {
			++board_at(x, y)->fox;
			++fox;
		}
	}
}

static int check_cell(int x, int y, bool found)
{
	struct board_cell *cell = board_at(x, y);
	if (found)
		--cell->visible;
	cell->animation = phase_per_sec;
	return cell->fox - cell->found;
}

/** Считает количество видимых лис, задавая анимацию для проверенных клеток. */
static void board_check(int x, int y)
{
	if (move < move_max)
		++move;
	else
		game_state = gs_finish;
	struct board_cell *open = board_at(x, y);
	if (open->open < INT_MAX)
		++open->open;
	const bool found = open->fox > open->found;
	if (found) {
		++open->found;
		++fox_found;
		if (fox_found == fox_count)
			game_state = gs_finish;
	}
	int visible = open->fox - open->found;;
	for (int i = 0; i < board_size; ++i) {
		if (i != y)
			visible += check_cell(x, i, found);
		if (i != x) {
			visible += check_cell(i, y, found);

			const int c1 = x - y;
			if (i - c1 >= 0 && i - c1 < board_size)
				visible += check_cell(i, i - c1, found);

			const int c2 = x + y;
			if (c2 - i >= 0 && c2 - i < board_size)
				visible += check_cell(i, c2 - i, found);
		}
	}
	open->visible = visible;
}

static bool board_over(float x, float y, uint32_t button, uint32_t state)
{
	// Делитель здесь и в вычислениях x100 y100 после скобок — размер стороны
	// игрового поля, равный меньшей стороне окна. Соответствует aspect_ratio.
	if (x >= 2.0f / aspect_ratio - 1.0f)
		return false;

	// x + 1.0f == 0                   => 0
	// x + 1.0f == 2.0f / aspect_ratio => board_size
	int x100 = (100 / 2) * aspect_ratio * board_size * (x + 1.0f);
	int y100 = (100 / 2) * aspect_ratio * board_size * (y + 1.0f/aspect_ratio);
	if (x100 % 100 > 4 && x100 % 100 < 97)
		board_cell_x = x100 / 100;
	if (y100 % 100 > 4 && y100 % 100 < 97)
		board_cell_y = y100 / 100;

	if (button && state && board_cell_x >= 0 && board_cell_y >= 0) {
		board_check(board_cell_x, board_cell_y);
	}
	return true;
}

static int cell_phase;

static inline void colorer_brd(struct vertex *restrict vert, struct color src, unsigned i)
{
	if (i) {
		vert->color = src;
	} else if (!cell_phase) {
		vert->color.r = src.r * 2.0f;
		vert->color.g = src.g * 2.0f;
		vert->color.b = src.b * 2.0f;
		vert->color.a = src.a;
	} else {
		float phase = (cell_phase > phase_per_sec/2
		            ? (phase_per_sec - cell_phase) : cell_phase)
		            / (float)(phase_per_sec/2);
		vert->color.r = src.r + (1 - src.r) * phase;
		vert->color.g = src.g + (1 - src.g) * phase;
		vert->color.b = src.b + (1 - src.b) * phase;
		vert->color.a = src.a;
	}
}

static inline void colorer_cf(struct vertex *restrict vert, struct color src, unsigned i)
{
	if (i) {
		vert->color = src;
	} else {
		vert->color.r = src.r;
		vert->color.g = src.g;
		vert->color.b = src.b;
		vert->color.a = 0.7f * src.a;
	}
}

static void board_draw(struct draw_ctx *restrict ctx, struct pos2d pos)
{
	const float step = 2.0f;
	const float w = board_size * aspect_ratio;
	struct vec4 at = {
		.y = w * pos.y - board_size + 1.0f,
		.w = w,
	};
	for (int yc = 0; yc < board_size; ++yc, at.y += step) {
		at.x = w * pos.x - board_size + 1.0f;
		for (int xc = 0; xc < board_size; ++xc, at.x += step) {
			struct board_cell *cell = board_at(xc, yc);
			if (cell->animation > 0)
				--cell->animation;
			cell_phase = cell->animation;
			bool hover = xc == board_cell_x && yc == board_cell_y;
			struct color cc = hover ? COLOR_CELL_HOVER : COLOR_CELL;
			if (cell->found)
				cc.r += 0.30f;
			poly_draw(&square094, at, colorer_brd, cc, ctx);
			if (cell->open > 0) {
				char num[2] = { cell->visible + '0', '\x00' };
				draw_text(num, &octagon150, at, colorer_cf, COLOR_CELL_TEXT, ctx);
			}
		}
	}
}

static void rectangle(struct draw_ctx *restrict ctx,
                      struct vec4 at, float hw, float hh, struct color color)
{
	if (ctx->stage) {
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

static float fsin(float a)
{
#if 0
	return sinf(a);
#else
#define SINCACHE_SIZE 256
	static float cache[2*SINCACHE_SIZE];
	const float moda = fmodf(a, 2.0f * PI);
	int indx = SINCACHE_SIZE + moda * ((float)SINCACHE_SIZE / (2.0f * PI));
	assert(indx >= 0);
	assert(indx < 2*SINCACHE_SIZE);
	if (!cache[indx]) {
		cache[indx] = sinf(moda);
		// Нулевое значение не ошибка, однако приводит к постоянным вычислениям.
		assert(cache[indx]);
	}
	return cache[indx];
#undef SINCACHE_SIZE
#endif
}

static float omega_title;

static inline void colorer_title(struct vertex *restrict vert, struct color src, unsigned i)
{
	if (!i) {
		vert->color = src;
	} else {
		vert->color.r = 0.5f * (1.0f + fsin(vert->pos.x + omega_title * i));
		vert->color.g = 0.5f * (1.0f + fsin(omega_title + i));
		vert->color.b = 0.5f * (1.0f + fsin(vert->pos.y + omega_title + i));
		vert->color.a = src.a;
	}
}

static void title(struct draw_ctx *restrict ctx, struct vec4 at)
{
	omega_title = omega_title < 2.0f*PI ? omega_title + PI/1024.0f : 0;
	rectangle(ctx, at, 4.53f, 2.5f, COLOR_BOX);
	static const char *const text[] = {
		"ОХОТА",
		"НА ЛИС",
	};
	text_lines(text, sizeof(text)/sizeof(*text), &octagon150, at,
	           colorer_title, COLOR_TITLE, ctx);
}

static void time_init()
{
	timespec_get(&start_time, TIME_UTC);
}

struct timespec time_from_start(void)
{
	struct timespec now;
#ifdef NDEBUG
	timespec_get(&now, TIME_UTC);
#else
	int r = timespec_get(&now, TIME_UTC);
#endif
	assert(r == TIME_UTC);
	const bool carry = now.tv_nsec < start_time.tv_nsec;
	return (struct timespec) {
		.tv_sec  = now.tv_sec  - start_time.tv_sec - carry,
		.tv_nsec = now.tv_nsec - start_time.tv_nsec + (carry ? 1000000000 : 0),
	};
}

static void score(struct draw_ctx *restrict ctx, struct vec4 at)
{
	rectangle(ctx, at, 4.3f, 7.5f, COLOR_BOX);

	assert(move <= 99);
	assert(fox_count <= 9);
	assert(fox_found <= 9);

	// Строки с информацией о партии формируем на первом этапе.
	static char playtime[6];
	static char movestr[3];
	static char foxc[4];
	if (!ctx->stage) {
		const struct timespec pt = time_from_start();
		static struct timespec last;
		if (game_state == gs_play)
			last = pt;
		time_t m = last.tv_sec / 60;
		long   s = last.tv_sec % 60;
		if (m > 99 || m < 0) {
			m = s = 99;
			game_state = gs_finish;
		}

		playtime[0] = '0' + m / 10;
		playtime[1] = '0' + m % 10;
		playtime[2] = game_state != gs_play || pt.tv_nsec < 1000000000/2 ? ':' : '\x01';
		playtime[3] = '0' + s / 10;
		playtime[4] = '0' + s % 10;
		playtime[5] = '\x0';

		movestr[0] = '0' + move / 10;
		movestr[1] = '0' + move % 10;
		movestr[2] = '\x00';

		foxc[0] = '0' + fox_found;
		foxc[1] = '/';
		foxc[2] = '0' + fox_count;
		foxc[3] = '\x00';
	}
	static const char *const text[][2] = {
		{ "ВРЕМЯ", playtime },
		{ "ХОДЫ",  movestr },
		{ "ЛИСЫ",  foxc },
	};
	const int pairs = sizeof(text)/sizeof(*text);
	for (int s = 0; s < pairs; ++s) {
		const float dy = 5.0f * (s - 0.5f * (pairs-1));
		text_lines(text[s], 2, &octagon150, (struct vec4){ at.x, at.y + dy, at.z, at.w },
				colorer_cf, COLOR_SCORE, ctx);
	}
}

struct button {
	float	left;
	float	right;
	float	top;
	float	bottom;
	bool 	over;   	///< Указатель над областью (сбрасывается при нажатии).
	bool 	pressed;	///< Кнопка нажата
	bool 	click;  	///< Кнопка только что нажата.
	bool 	release;	///< Кнопка только что отпущена.
};

static inline
void button_area_set(struct button *aa, struct vec4 at, float hw, float hh)
{
	aa->left   = (at.x - hw) / at.w;
	aa->right  = (at.x + hw) / at.w;
	aa->top    = (at.y - hh) / at.w;
	aa->bottom = (at.y + hh) / at.w;
}

static inline
bool button_over(struct button *b, float x, float y, uint32_t button, uint32_t state)
{
	b->click   = false;
	b->release = false;
	if (b->left > x || x > b->right || b->top > y || y > b->bottom) {
		b->over    = false;
		b->pressed = false;
		return false;
	}
	if (button) {
		if (state && !b->pressed)
			b->click = true;
		if (!state && b->pressed)
			b->release = true;
		b->pressed = state;
		if (!state)
			b->over = true;
	} else if (!b->pressed)
		b->over = true;
	return true;
}

static struct button button_start, button_exit;

static void menu(struct draw_ctx *restrict ctx, struct vec4 at)
{
	rectangle(ctx, at, 4.5f, 2.8f, COLOR_BOX);
	const float dy = 1.5f;

	button_area_set(&button_start, (struct vec4){ at.x, at.y - dy, at.z, at.w }, 4.3f, 1.1f);
	if (button_start.over)
		rectangle(ctx, (struct vec4){ at.x, at.y - dy, at.z, at.w }, 4.3f, 1.1f, COLOR_HOVER);
	draw_text(game_state == gs_play ? "СТОП":"СТАРТ", &octagon150, (struct vec4){ at.x, at.y - dy, at.z, at.w },
	          colorer_cf, game_state == gs_play ? COLOR_STOP : COLOR_START, ctx);

	button_area_set(&button_exit, (struct vec4){ at.x, at.y + dy, at.z, at.w }, 4.3f, 1.1f);
	if (button_exit.over)
		rectangle(ctx, (struct vec4){ at.x, at.y + dy, at.z, at.w }, 4.3f, 1.1f, COLOR_HOVER);
	draw_text("ВЫХОД", &octagon150, (struct vec4){ at.x, at.y + dy, at.z, at.w },
	          colorer_cf, COLOR_EXIT, ctx);
}

static float omega_intro;
static struct color color_rt[9];

static inline void colorer_rt(struct vertex *restrict vert, struct color src, unsigned i)
{
	vert->color = color_rt[i];
}

static inline void colorer_c(struct vertex *restrict vert, struct color src, unsigned i)
{
	if (i) {
		vert->color = src;
	} else {
		vert->color.r = src.r * 0.5f * (1.0f + fsin(vert->pos.x + omega_intro));
		vert->color.g = src.g * 0.5f * (1.0f + fsin(vert->pos.y + omega_intro));
		vert->color.b = src.b * 0.5f * (1.0f + fsin(vert->pos.x + vert->pos.y + omega_intro));
		vert->color.a = src.a;
	}
}

static void intro(struct draw_ctx *restrict ctx, struct pos2d at)
{
	omega_intro = omega_intro < 2.0f*PI ? omega_intro + PI/256.0f : 0;
	static int seconds;
	if (!ctx->stage) {
		if (--seconds < 0)
			seconds = phase_per_sec * 20;
	}
	if (seconds > phase_per_sec * 5) {
		const static char *const rules[] = {
			"В случайных клетках",
			"располагаются \"лисы\" -",
			"радиопередатчики,",
			"посылающие в эфир",
			"сигнал \"я здесь\".",
			"\"Охотник\" вооружен",
			"пеленгатором, имеющим",
			"направленную антенну,",
			"так что сигналы \"лис\"",
			"принимаются по вертикали,",
			"горизонтали и диагоналям.",
			"Цель:",
			"обнаружить \"лис\" за",
			"минимальное число ходов.",
			"Найденная \"лиса\"",
			"снимается с поля.",
		};
		const float iw = 20.0f * aspect_ratio;
		const struct vec4 at4 = { at.x * iw, at.y * iw, 0.0f, iw };
		rectangle(ctx, at4, 19.5f, 19.5f, COLOR_BOX);
		struct color ca = COLOR_INTRO;
		if (seconds < phase_per_sec * 6)
			ca.a = ca.a * (seconds - phase_per_sec * 5) / (float)phase_per_sec;
		else if (seconds > phase_per_sec * 19)
			ca.a = ca.a * (phase_per_sec * 20 - seconds) / (float)phase_per_sec;
		color_rt[0] = ca;
		for (int i = 1; i <= 8; ++i) {
			color_rt[i].r = 0.5f * (1.0f + fsin(omega_intro + i));
			color_rt[i].g = 0.5f * (1.0f + fsin(omega_intro + i));
			color_rt[i].b = 0.5f * (1.0f + fsin(omega_intro + i));
			color_rt[i].a = ca.a;
		}
		text_lines(rules, sizeof(rules)/sizeof(*rules), &octagon150, at4,
		           colorer_rt, ca, ctx);
	} else {
		const static char *const authors[] = {
			"Авторы",
			"",
			"ИДЕЯ/МК-61\x17",
			"\x17А. Несчетный",
			"",
			"Реализация\x12\xd",
			"\x1e\xbС. Трусов",
			"",
			"Музыка\x1e\x16",
			"\x12Е. Столяренко",
		};
		const float iw = 13.5f * aspect_ratio;
		const struct vec4 at4 = { at.x * iw, at.y * iw, 0.0f, iw };
		rectangle(ctx, at4, 13.0f, 13.0f, COLOR_BOX_A);
		struct color ca = COLOR_AUTHORS;
		if (seconds < phase_per_sec)
			ca.a = ca.a * seconds / (float)phase_per_sec;
		else if (seconds > phase_per_sec * 4)
			ca.a = ca.a * (phase_per_sec * 5 - seconds) / (float)phase_per_sec;
		text_lines(authors, sizeof(authors)/sizeof(*authors), &octagon150, at4,
		           colorer_c, ca, ctx);
	}
}

static float omega_bk;

static inline void colorer(struct vertex *restrict vert, struct color src, unsigned i)
{
	if (!i) {
		vert->color = src;
	} else {
		vert->color.r = src.r * (1.0f + fsin(vert->pos.x + omega_bk + i));
		vert->color.g = src.g * (1.0f + fsin(vert->pos.y + omega_bk + i));
		vert->color.b = src.b * (1.0f + fsin(vert->pos.x + vert->pos.y + omega_bk + i));
		vert->color.a = src.a;
	}
}

static void background(struct draw_ctx *restrict ctx)
{
	omega_bk = omega_bk < 2.0f*PI ? omega_bk + PI/512.0f : 0;
	const int dot_cnt = aspect_ratio * board_size * 3;
	for (int y = -dot_cnt/aspect_ratio + 1; y < dot_cnt/aspect_ratio; y += 2)
		for (int x = -dot_cnt + 1; x < dot_cnt; x += 2)
			poly_draw(&square108, (struct vec4){ x, y, 0, dot_cnt },
			          colorer, COLOR_BACKGROUND, ctx);
}

static void game_start(void)
{
	game_state = gs_play;
	time_init();
	srand(start_time.tv_nsec ^ start_time.tv_sec);
	board_init();
	ay_music_select(1);
}

static void game_stop(void)
{
	game_state = gs_intro;
	ay_music_select(0);
}

static bool draw_frame(void *p)
{
	ay_music_continue(5);

	struct vk_context *vk = p;
	VkResult r = vk_acquire_frame(vk);

	// На стадии 0 вычисляем размер буферов, на следующей их заполняем.
	unsigned total_indices;
	unsigned total_vertices;
	for (struct draw_ctx dc = {0}; dc.stage <= 1; ++dc.stage) {

		struct vertex	*vert_buf = NULL;
		vert_index   	*indx_buf = NULL;
		if (dc.stage) {
			// Округляем немного вверх, что бы избежать новых распределений памяти.
			unsigned vtcs = (total_vertices | ((1 << 5) - 1)) - 1;
			unsigned idcs = (total_indices  | ((1 << 6) - 1)) - 1;
			r = vk_begin_vertex_buffer(vk, vtcs * sizeof(struct vertex), &vert_buf);
			r = vk_begin_index_buffer(vk, idcs * sizeof(vert_index), &indx_buf);
		}
		dc.vert_buf = vert_buf;
		dc.indx_buf = indx_buf;

		background(&dc);

		// TODO при обработке ввода координата пока не учитывается.
		const struct pos2d board_center = {
			.x = 1.0f / aspect_ratio - 1.0f,
			.y = 0.0f,
		};
		switch (game_state) {
		case gs_play:
		case gs_finish:
			board_draw(&dc, board_center);
			break;
		case gs_intro:
			intro(&dc, board_center);
			break;
		}

		const float tw = 18.5f;
		const float twa = tw / aspect_ratio;
		title(&dc, (struct vec4){ twa, -0.7f * twa, 0.0f, tw });

		const float sw = 15.0f * aspect_ratio;
		score(&dc, (struct vec4){ sw/aspect_ratio, 0.05f * sw, 0.0f, sw });

		const float mw = 21.0f * aspect_ratio;
		const float mwa = mw / aspect_ratio;
		menu(&dc, (struct vec4){ mwa, 0.8f * mwa, 0.0f, mw });

		if (dc.stage) {
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

static bool pointer_click(struct window *window, double x, double y,
                          const char **cursor_name, uint32_t button, uint32_t state)
{
	button_start.over = false;
	button_exit.over  = false;
	board_cell_x = -1;
	board_cell_y = -1;
	if (x >= 0) {
		float xh = (2.0 * x / window->width) - 1.0;
		float yh = ((2.0 * y / window->height) - 1.0) / window->aspect_ratio;

		if (game_state == gs_play && board_over(xh, yh, button, state)) {
			goto over;
		}
		if (button_over(&button_start, xh, yh, button, state)) {
			if (button_start.release)
				switch (game_state) {
				case gs_finish:
				case gs_intro:	game_start(); break;
				case gs_play: 	game_stop();  break;
				}
			goto over;
		}
		if (button_over(&button_exit, xh, yh, button, state)) {
			if (button_exit.release)
				window->close = true;
			goto over;
		}
	}
	return false;
over:
	*cursor_name = "left_ptr";
	return true;
}

static
bool pointer_over(struct window *window, double x, double y, const char **cursor_name)
{
	return pointer_click(window, x, y, cursor_name, 0, 0);
}

void touch(struct window *window, double x, double y)
{
	const char *dummy_cursor;
	window->ctrl->click(window, x, y, &dummy_cursor, BTN_LEFT, 1);
	window->ctrl->click(window, x, y, &dummy_cursor, BTN_LEFT, 0);
	board_cell_x = -1;
	board_cell_y = -1;
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
	.touch  	= touch,
};

int main(int argc, char *argv[])
{
	printf("«%s» версия " APP_VERSION ".\n", game_name);

	bool music = ay_music_init() >= 0;
	if (music)
		ay_music_play();

	if (!wayland_init())
		return 1;

	poly_init(&square094, 0.94f * 1.414213562f); // √2
	poly_init(&square108, 1.08f * 1.414213562f); // √2
	poly_init(&octagon150, 1.5);

	if (vk_init() != VK_SUCCESS)
		return 2;

	time_init();

	struct window window = {
		.ctrl 	= &controller,
		.render	= &vulkan,
		.title	= game_name,
		.width	= 800,
//		.height	= 600,
		.border = 10,
		.aspect_ratio = aspect_ratio,
//		.constant_aspect_ratio = true,
	};
	window_create(&window);

	while(!window.close) {
		if (!wayland_dispatch())
			break;
	}

	window_destroy(&window);

	vk_stop();
	wayland_stop();

	if (music)
		ay_music_stop();

	return 0;
}

