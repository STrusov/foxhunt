
#include <assert.h>
#include "text.h"

enum {
	glyph_cyrillic = 1 + ':' - ' ',
	glyph_count  = 1 + glyph_cyrillic + L'Я' - L'А',
};

static const uint8_t font[glyph_count][glyph_height] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	// пробел
	{ 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00 },	// !
	{ 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	// "
	{ 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },	// # №
	{ 0x00, 0x00, 0x02, 0x05, 0x02, 0x00, 0x07, 0x00 },	// $ №
	{ 0x00, 0x11, 0x02, 0x04, 0x08, 0x11, 0x00, 0x00 },	// %
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	// &
	{ 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	// '
	{ 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x00 },	// (
	{ 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x00 },	// )
	{ 0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00, 0x00 },	// *
	{ 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, 0x00 },	// +
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02 },	// ,
	{ 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00 },	// -
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 },	// .
	{ 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00 },	// /
	{ 0x3e, 0x41, 0x41, 0x41, 0x41, 0x41, 0x3e, 0x00 },	// 0
	{ 0x08, 0x18, 0x28, 0x48, 0x08, 0x08, 0x7f, 0x00 },	// 1
	{ 0x3e, 0x41, 0x01, 0x01, 0x3e, 0x40, 0x7f, 0x00 },	// 2
	{ 0x7e, 0x01, 0x01, 0x3e, 0x01, 0x01, 0x7e, 0x00 },	// 3
	{ 0x42, 0x42, 0x42, 0x42, 0x7f, 0x02, 0x02, 0x00 },	// 4
	{ 0x7f, 0x40, 0x7e, 0x01, 0x01, 0x41, 0x3e, 0x00 },	// 5
	{ 0x3e, 0x41, 0x40, 0x7e, 0x41, 0x41, 0x3e, 0x00 },	// 6
	{ 0x7f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x00 },	// 7
	{ 0x3e, 0x41, 0x41, 0x3e, 0x41, 0x41, 0x3e, 0x00 },	// 8
	{ 0x3e, 0x41, 0x41, 0x3f, 0x01, 0x41, 0x3e, 0x00 },	// 9
	{ 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 },	// :
	{ 0x0e, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00 },	// А
	{ 0x1f, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x1e, 0x00 },	// Б
	{ 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00 },	// В
	{ 0x1f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 },	// Г
	{ 0x0e, 0x12, 0x22, 0x22, 0x22, 0x22, 0x7f, 0x41 },	// Д
	{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f, 0x00 },	// Е
	{ 0x49, 0x2a, 0x1c, 0x08, 0x1c, 0x2a, 0x49, 0x00 },	// Ж
	{ 0x0e, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0e, 0x00 },	// З
	{ 0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x00 },	// И
	{ 0x15, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x00 },	// Й
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },	// К
	{ 0x07, 0x09, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00 },	// Л
	{ 0x41, 0x63, 0x55, 0x49, 0x41, 0x41, 0x41, 0x00 },	// М
	{ 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00 },	// Н
	{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },	// О
	{ 0x1f, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x00 },	// П
	{ 0x1e, 0x11, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x00 },	// Р
	{ 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, 0x00 },	// С
	{ 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },	// Т
	{ 0x11, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e, 0x00 },	// У
	{ 0x08, 0x3e, 0x49, 0x49, 0x49, 0x3e, 0x08, 0x00 },	// Ф
	{ 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x00 },	// Х
	{ 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x3f, 0x01 },	// Ц
	{ 0x11, 0x11, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x00 },	// Ч
	{ 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x7f, 0x00 },	// Ш
	{ 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0xff, 0x01 },	// Щ
	{ 0x30, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x1e, 0x00 },	// Ъ
	{ 0x41, 0x41, 0x41, 0x79, 0x45, 0x45, 0x79, 0x00 },	// Ы
	{ 0x10, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x1e, 0x00 },	// Ь
	{ 0x0e, 0x11, 0x01, 0x07, 0x01, 0x11, 0x0e, 0x00 },	// Э
	{ 0x4e, 0x51, 0x51, 0x71, 0x51, 0x51, 0x4e, 0x00 },	// Ю
	{ 0x0f, 0x11, 0x11, 0x0f, 0x05, 0x09, 0x11, 0x00 },	// Я
};

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

/** Определяет количество пикселей в глифе. */
static inline unsigned glyphpopc(unsigned idx)
{
	static unsigned char popc[glyph_count];
	if (!popc[idx]) {
		unsigned n = 0;
		for (int l = 0; l < glyph_height; ++l) {
			uint8_t line = font[idx][l];
			// См. Генри Уоррен "Алгоритмические трюки для программистов"
			while (line) {
				++n;
				line &= line - 1;
			}
		}
		popc[idx] = n + 1; // исключаем повторное вычисление для пробелов
	}
	return popc[idx] - 1;
}

void draw_text(const char *str, const struct polygon *poly, struct vec4 at,
               void(painter)(struct vertex*, struct color), struct color color,
               int stage, struct draw_ctx *restrict ctx)
{
	int cnt;
	// Предварительно подготавливаем индексы в массиве font и ширину глифов.
	unsigned glidx[32] = {};
	unsigned width[32];
	unsigned popc = 0;
	int line_width = 0;
	for (cnt = 0; *str; ++str, ++cnt) {
		assert(cnt <= 32);
		// Юникод диапазон А..Я в UTF-8 кодируется 0xd0 0x90 .. 0xd0 0xaf
		if (*str == (char)0xd0) {
			char c2 = 0x7f & *++str;
			assert(c2 >= 0x10 && c2 <= 0x2f);
			glidx[cnt] = glyph_cyrillic + c2 - 0x10;
		} else if (*str >= ' ') {
			glidx[cnt] = *str - ' ';
		} else {
			glidx[cnt] = ' ' - ' ';
			width[cnt] = *str;
			goto calc_line_width;
		}
		if (stage) {
			width[cnt] = glyphwidth(glidx[cnt]);
calc_line_width:
			line_width += width[cnt] + 1; // межсимвольный интервал.
		} else {
			popc += glyphpopc(glidx[cnt]);
		}
	}
	if (!stage) {
		ctx->vert_buf += popc * poly->vert_count;
		ctx->indx_buf += popc * 3 * poly->tri_count;
		return;
	}
	line_width -= 1;
	int x0 = -line_width - 1;
	for (int c = 0; c < cnt; ++c) {
		for (int l = 0; l < glyph_height; ++l) {
			unsigned line = font[glidx[c]][l];
			for (int v = 2 * width[c]; v ; v -= 2) {
				if (line & 1) {
					const struct vec4 coord = {
						.x = at.x * glyph_height + (x0 + v),
						.y = at.y * glyph_height + (2 * l - glyph_height + 2),
						.z = at.z * glyph_height,
						.w = at.w * glyph_height,
					};
					poly_draw(poly, coord, painter, color, stage, ctx);
				}
				line >>= 1;
			}
		}
		x0 += 2 * width[c] + 2;
	}
}

void text_lines(const char *const text[], int lines, const struct polygon *poly, struct vec4 at,
               void(painter)(struct vertex*, struct color), struct color color,
               int stage, struct draw_ctx *restrict ctx)
{
	assert(lines > 0);
	for (int s = 0; s < lines; ++s) {
		float dy = 2.0f * (glyph_height + 1.0f)/glyph_height * (s - 0.5f*(lines-1));
		draw_text(text[s], poly, (struct vec4){ at.x, at.y + dy, at.z, at.w },
		          painter, color, stage, ctx);
	}
}

