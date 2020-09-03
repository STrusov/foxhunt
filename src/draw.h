
#pragma once

#include <stdint.h>

#define PI 3.14159f

typedef uint32_t vert_index;
typedef unsigned fast_index;

struct pos2d {
	float	x;
	float	y;
};

struct vec4 {
	float	x;
	float	y;
	float	z;
	float	w;
};

struct color {
	float	r;
	float	g;
	float	b;
	float	a;
};

struct vertex {
	struct vec4 	pos;
	struct color	color;
};

struct draw_ctx {
	struct vertex 	*restrict vert_buf;
	vert_index    	*restrict indx_buf;
	vert_index    	base;
};
