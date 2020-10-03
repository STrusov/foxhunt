
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include "window.h"

/**
 * Ключевой объект для связи с сервером.
 */
static struct xcb_connection_t	*connection;

/**
 * Экран для отображения окна.
 */
static struct xcb_screen_t    	*screen;


static void draw_frame(struct window *window)
{
	assert(window->render->draw_frame);
	window->render->draw_frame(window->render_ctx);
}


static xcb_atom_t get_atom_by_name(const char *name)
{
	xcb_intern_atom_cookie_t c = xcb_intern_atom(connection, 0, strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, c, NULL);
	xcb_atom_t r = reply ? reply->atom : XCB_ATOM_NONE;
#ifndef NDEBUG
	printf("Атом %s ID = %u\n", name, r);
#endif
	free(reply);
	return r;
}

enum atom {
	atom_first,
	net_wm_name = atom_first,
	net_wm_icon_name,
	net_wm_window_type,
	net_wm_window_type_normal,
	utf8_string,
	atom_max
};

// См. Extended Window Manager Hints (EWMH).
static struct { const char *const name; xcb_atom_t id; } atom[atom_max] = {
	[net_wm_name]              	= { .name = "_NET_WM_NAME" },
	[net_wm_icon_name]         	= { .name = "_NET_WM_ICON_NAME" },
	[net_wm_window_type]       	= { .name = "_NET_WM_WINDOW_TYPE" },
	[net_wm_window_type_normal]	= { .name = "_NET_WM_WINDOW_TYPE_NORMAL" },
	[utf8_string]              	= { .name = "UTF8_STRING" },
};

static void atom_init_map()
{
	for (enum atom i = atom_first; i < atom_max; ++i)
		atom[i].id = get_atom_by_name(atom[i].name);
}

bool xcb_init(void)
{
	int preferred = 0;
	xcb_connection_t *c = xcb_connect(NULL, &preferred);
	if (xcb_connection_has_error(c)) {
		xcb_disconnect(c);
	} else {
		connection = c;
		xcb_screen_iterator_t si = xcb_setup_roots_iterator(xcb_get_setup(c));
		while (preferred > 0 && si.rem) {
			xcb_screen_next(&si);
			--preferred;
		}
		if (!preferred)
			screen = si.data;
		atom_init_map();
	}
	return connection && screen;
}

void xcb_stop(void)
{
	xcb_disconnect(connection);
	connection = NULL;
}

// TODO
static struct window *window_object;

bool window_create(struct window *window)
{
	window_geometry(window);

	window->window = xcb_generate_id(connection);
	const xcb_cw_t value_mask = XCB_CW_EVENT_MASK;
	const xcb_event_mask_t value_list[] = {
		XCB_EVENT_MASK_EXPOSURE,
	};
	xcb_void_cookie_t ck;
	ck = xcb_create_window_checked(connection, XCB_COPY_FROM_PARENT, window->window, screen->root,
	                               0, 0, window->width, window->height, window->border,
	                               XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
	                               value_mask, value_list);
	xcb_generic_error_t *r = xcb_request_check(connection, ck);
	if (!r) {
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    atom[net_wm_window_type].id, XCB_ATOM_ATOM,
		                    32, 1, &atom[net_wm_window_type_normal].id);
		size_t tl = strlen(window->title);
#if 0
		// Вывод UTF-8 строк как XCB_ATOM_STRING происходит кракозябрами.
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, tl, window->title);
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8, tl, window->title);
#endif
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    atom[net_wm_name].id, atom[utf8_string].id, 8, tl, window->title);
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    atom[net_wm_icon_name].id, atom[utf8_string].id, 8, tl, window->title);
#if 0
		char *cl = calloc(2*tl+2, sizeof(*cl));
		if (cl) {
			strcpy(cl, window->title);
			strcpy(cl + tl + 1, window->title);
			xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
			                    XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, 2*tl+1, cl);
			free(cl);
		}
#endif
		assert(window->render);
		assert(window->render->create);
		window->render->create(connection, window->window, window->width,
		                       window->height, &window->render_ctx);
		if (window->render_ctx) {
//			draw_frame(window);
			xcb_map_window(connection, window->window);
			xcb_flush(connection);
		}

		window_object = window;

	}
	return window->render_ctx;
}

bool xcb_dispatch(void)
{
	draw_frame(window_object);
	return true;

	xcb_generic_event_t *ev = xcb_poll_for_event(connection);
	if (!ev)
		return false;

	switch (ev->response_type & ~0x80) {
		case XCB_EXPOSE: {
			xcb_expose_event_t *ex = (xcb_expose_event_t*)ev;

			break;
		}
	}
	free(ev);
	return true;
}

void window_destroy(struct window *window)
{
	if (window->render->destroy && window->render_ctx)
		window->render->destroy(window->render_ctx);

	xcb_destroy_window(connection, window->window);
}
