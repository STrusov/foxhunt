/**\file
 * \brief	Реализация клиента X11 (XCB).
 *
 *  Не в полной мере соответствует Wayland клиенту:
 *  - поддерживается только одно окно;
 *  - изменение размеров окна в реальном времени оставляет желать лучшего;
 *  - при скрытии окна музыка продолжает играть (Gnome).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input-event-codes.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_icccm.h>
//#include <xcb/xfixes.h>
#include "window.h"

/**
 * Ключевой объект для связи с сервером.
 */
static struct xcb_connection_t	*connection;

/**
 * Экран для отображения окна.
 */
static struct xcb_screen_t    	*screen;

/** Определяют формат изображения и цветовую глубину. */
static struct xcb_visualtype_t	*visual;
static xcb_colormap_t         	colormap;
static uint8_t                	visual_depth;

static xcb_cursor_context_t   	*cursor_context;

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
	motif_wm_hints,
	utf8_string,
	wm_delete_window,
	wm_protocols,
	atom_max
};

// См. Extended Window Manager Hints (EWMH).
static struct { const char *const name; xcb_atom_t id; } atom[atom_max] = {
	[net_wm_name]              	= { .name = "_NET_WM_NAME" },
	[net_wm_icon_name]         	= { .name = "_NET_WM_ICON_NAME" },
	[net_wm_window_type]       	= { .name = "_NET_WM_WINDOW_TYPE" },
	[net_wm_window_type_normal]	= { .name = "_NET_WM_WINDOW_TYPE_NORMAL" },
	[motif_wm_hints]           	= { .name = "_MOTIF_WM_HINTS" },
	[utf8_string]              	= { .name = "UTF8_STRING" },
	[wm_delete_window]         	= { .name = "WM_DELETE_WINDOW" },
	[wm_protocols]             	= { .name = "WM_PROTOCOLS" },
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
		if (!preferred) {
			screen = si.data;
			// Ищем подходящий режим для отображения ARGB (32 разряда)
			// либо используем базовый.
			xcb_depth_iterator_t di;
			xcb_visualtype_iterator_t vi;
			for (di = xcb_screen_allowed_depths_iterator(screen); di.rem; xcb_depth_next(&di))
				for (vi = xcb_depth_visuals_iterator(di.data); vi.rem; xcb_visualtype_next(&vi)) {
					if (di.data->depth == 32 && vi.data->_class == XCB_VISUAL_CLASS_TRUE_COLOR) {
						visual = vi.data;
						visual_depth  = di.data->depth;
						break;
					}
					if (!visual && screen->root_visual == vi.data->visual_id) {
						visual = vi.data;
						visual_depth  = di.data->depth;
					}
				}
			colormap = xcb_generate_id(connection);
			xcb_create_colormap(connection, XCB_COLORMAP_ALLOC_NONE, colormap, screen->root, visual->visual_id);
		}
		atom_init_map();
	}
	return connection && screen && visual;
}

void xcb_stop(void)
{
	xcb_disconnect(connection);
	connection = NULL;
}

///\param name указатель на _статически_ аллоцированную строку (сохраняется без strdup)
///      либо NULL — для скрытия курсора (не реализовано).
static bool set_cursor(struct window * window, const char *name)
{
	static const char empty[0] = {};
	if (!name)
		name = empty;
	if (name != window->cursor_name) {
		if (name != empty) {
			if (!cursor_context)
				xcb_cursor_context_new(connection, screen, &cursor_context);
			if (!cursor_context)
				return false;
			// Wayland загружает курсоры разом и хранит, по запросу выбирая из
			// набора. В данном случае в угоду простоте обходимся без кеширования.
			xcb_cursor_t cursor = xcb_cursor_load_cursor(cursor_context, name);
			xcb_change_window_attributes(connection, window->window, XCB_CW_CURSOR,
			                             &(uint32_t[]){cursor});
			xcb_free_cursor(connection, cursor);
		} else {
			// TODO
			//xcb_xfixes_hide_cursor(connection, window->window);
		}
		window->cursor_name = name;
	}
	return true;
}

static void draw_frame(struct window *window)
{
	assert(window->render->draw_frame);
	bool next = false;
	if (window->visible && !window->close)
		next = window->render->draw_frame(window->render_ctx);
	if (next) {
		const struct xcb_expose_event_t ex = {
			.window = window->window,
			.response_type = XCB_EXPOSE,
			.x = 0,
			.y = 0,
			.width = window->width,
			.height = window->height,
		};
		xcb_send_event(connection, 0, window->window, XCB_EVENT_MASK_EXPOSURE, (const char*)&ex);
	}
}

// TODO Для поддержки более одного окна следует реализовать адекватные
// window_set_ptr() и window_get_ptr(), а так же предусмотреть в
// xcb_dispatch() накопление соответствующее количеству окон сообщений expose и resize.
static struct window *window_object;

/** Ассоциирует указатель на объект окна с идентификатором X11. */
static void window_set_ptr(struct window *window)
{
	window_object = window;
}

/** Получает указатель на объект из оконного идентификатора X11. */
static struct window *window_get_ptr(xcb_window_t id)
{
	assert(window_object->window == id);
	return window_object;
}


static void on_resize(xcb_resize_request_event_t *e)
{
	struct window *window = window_get_ptr(e->window);
	// ICCC: Clients must not respond to being resized by attempting to resize
	// themselves to a better size.
	const uint32_t size[] = { e->width, e->height };
	xcb_configure_window(connection, e->window, XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, size);
	if (window->width != e->width || window->height != e->height) {
		window->width  = e->width;
		window->height = e->height;
		window->render->resize(window->render_ctx, window->width, window->height);
	}
}

/** Получает Input event code из xcb_button_t */
static inline unsigned ec_from_xcb(xcb_button_t b)
{
	switch(b) {
	case 1:	return BTN_LEFT;
	case 2:	return BTN_RIGHT;
	case 3:	return BTN_MIDDLE;
	case 4:	return BTN_SIDE;
	case 5:	return BTN_EXTRA;
	case 6:	return BTN_FORWARD;
	case 7:	return BTN_BACK;
	case 8:	return BTN_TASK;
	default:
		assert(0);
		return KEY_RESERVED;
	}
}

static void on_pointer_move(xcb_motion_notify_event_t *e)
{
	struct window *window = window_get_ptr(e->event);
	const char *cursor_name = "hand1";
	if (window->moving_by_client_area) {
		const uint32_t xy[] = {
			e->root_x - window->button_x,
			e->root_y - window->button_y,
		};
		xcb_configure_window(connection, e->event, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, xy);
		cursor_name = "grabbing";
	}
	else if (window->ctrl && window->ctrl->hover)
		window->ctrl->hover(window, e->event_x, e->event_y, &cursor_name);
#ifdef NDEBUG
	set_cursor(window, cursor_name);
#else
	const bool cursor_selected = set_cursor(window, cursor_name);
#endif
	assert(cursor_selected);
}

static void on_pointer_button(xcb_button_press_event_t *e, bool pressed)
{
	struct window *window = window_get_ptr(e->event);
	const char unchanged[0] = {};
	const char *cursor_name = unchanged;
	bool handled = false;
	if (window->ctrl && window->ctrl->click) {
		handled = window->ctrl->click(window, e->event_x, e->event_y, &cursor_name,
		                              ec_from_xcb(e->detail), pressed);
	}
	if (!handled) {
		// TODO в Mutter смещение по y на величину заголовка (?)
		window->button_x = e->event_x;
		window->button_y = e->event_y;
		window->moving_by_client_area = pressed;
		cursor_name = pressed ? "grabbing" : "hand1";
	}
	if (cursor_name != unchanged) {
#ifndef NDEBUG
		const bool cursor_selected = set_cursor(window, cursor_name);
#endif
		assert(cursor_selected);
	}
}

bool window_create(struct window *window)
{
	window_geometry(window);

	window->window = xcb_generate_id(connection);
	// XCB_CW_BORDER_PIXEL необходим для colormap при 32-х разрядном цвете.
	const xcb_cw_t value_mask = XCB_CW_BACK_PIXMAP
	                          | XCB_CW_BORDER_PIXEL
//	                          | XCB_CW_OVERRIDE_REDIRECT
	                          | XCB_CW_EVENT_MASK
	                          | XCB_CW_COLORMAP;
	xcb_event_mask_t event_mask = XCB_EVENT_MASK_EXPOSURE|XCB_EVENT_MASK_VISIBILITY_CHANGE;
	if (window->ctrl) {
		if (window->ctrl->click)
			event_mask |= XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE;
		if (window->ctrl->hover)
			event_mask |= XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_MOTION;
		///\see xcb_dispatch()
		if (window->render->resize)
			event_mask |= XCB_EVENT_MASK_RESIZE_REDIRECT;
	}
	const uint32_t value_list[] = {
		XCB_BACK_PIXMAP_NONE,
		0,
//		1,
		event_mask,
		colormap,
	};
	xcb_void_cookie_t ck;
	ck = xcb_create_window_checked(connection, visual_depth, window->window, screen->root,
	                               0, 0, window->width, window->height, 0,
	                               XCB_WINDOW_CLASS_INPUT_OUTPUT, visual->visual_id,
	                               value_mask, value_list);
	xcb_generic_error_t *r = xcb_request_check(connection, ck);
	if (!r) {
		xcb_size_hints_t hints = {
			.flags     	= XCB_ICCCM_SIZE_HINT_P_MIN_SIZE,
			.min_width 	= 2 * window->border,
			.min_height	= 2 * window->border,
		};
		if (window->aspect_ratio) {
			hints.flags |= XCB_ICCCM_SIZE_HINT_P_ASPECT;
			hints.min_aspect_num = hints.max_aspect_num = window->width;
			hints.min_aspect_den = hints.max_aspect_den = window->height;
		}
		xcb_icccm_set_wm_size_hints(connection, window->window, XCB_ATOM_WM_NORMAL_HINTS, &hints);
		if (1) {
			// Скрываем заголовок, оставляю бордюр для изменения размера.
			xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
			                    atom[motif_wm_hints].id, XCB_ATOM_ATOM,
			                    32, 5, (uint32_t[5]){2, 0, 2});
		}
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    atom[net_wm_window_type].id, XCB_ATOM_ATOM,
		                    32, 1, &atom[net_wm_window_type_normal].id);
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window->window,
		                    atom[wm_protocols].id, XCB_ATOM_ATOM,
		                    32, 1, &atom[wm_delete_window].id);
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
			xcb_map_window(connection, window->window);
			xcb_flush(connection);
		}
		window_set_ptr(window);
	}
	return window->render_ctx;
}

bool xcb_dispatch(void)
{
	bool flush = false;
	// Диспетчеру приходится совместить обработку асинхронных сообщений сервера
	// с необходимостью синхронизации вывода с развёрткой дисплея.
	// Поскольку Vulkan при смене отображаемых кадров выполняет синхронизацию,
	// достаточно использовать ожидание в vk_acquire_frame() без усложнения
	// работой с расширениям XSync. Однако, во время паузы при синхронизации
	// могут поступать сообщения от устройств ввода, а так же иные.
	// В цикле фиксируем факт прихода сообщений XCB_EXPOSE и XCB_RESIZE_REQUEST,
	// выбираем накопившиеся сообщения и обрабатываем их. После чего, если надо,
	// обрабатываем вывод/изменение размера.
	// XCB_CONFIGURE_REQUEST так же требует дополнительной синхронизации,
	// потому используем XCB_RESIZE_REQUEST.
	xcb_expose_event_t         *expose = NULL;
	xcb_resize_request_event_t *resize = NULL;
	xcb_generic_event_t *e = xcb_wait_for_event(connection);
	while (e) {
		switch (e->response_type & 0x7f) {
		// xcb_create_window_checked(). Рендер ещё не инициализирован.
		case XCB_CREATE_NOTIFY:
			break;
		// xcb_map_window(). Для окна XCB_WINDOW_CLASS_INPUT_OUTPUT
		// генерируется XCB_EXPOSE, когда окно становится видимым.
		case XCB_MAP_NOTIFY:
			break;
		// TODO В Gnome не актуально: приходит однократно перед XCB_EXPOSE.
		case XCB_VISIBILITY_NOTIFY:
			flush = true;
			xcb_visibility_notify_event_t *ev = (xcb_visibility_notify_event_t*)e;
			struct window *window = window_get_ptr(ev->window);
			window->visible = ev->state != XCB_VISIBILITY_FULLY_OBSCURED;
			break;
		case XCB_CLIENT_MESSAGE:
			if (atom[wm_delete_window].id == ((xcb_client_message_event_t*)e)->data.data32[0])
				window_get_ptr(((xcb_client_message_event_t*)e)->window)->close = true;
			break;
		case XCB_BUTTON_PRESS:
			on_pointer_button((xcb_button_press_event_t*)e, true);
			flush = true;
			break;
		case XCB_BUTTON_RELEASE:
			on_pointer_button((xcb_button_release_event_t*)e, false);
			flush = true;
			break;
		case XCB_MOTION_NOTIFY:
			on_pointer_move((xcb_motion_notify_event_t *)e);
			flush = true;
			break;
		case XCB_EXPOSE:
			// Может прийти при XCB_RESIZE_REQUEST
			if (expose) {
				assert(expose->window == ((xcb_expose_event_t*)e)->window);
				free(expose);
			}
			expose = (xcb_expose_event_t*)e;
			e = NULL;
			break;
		// Последовательность согласно Inter-Client Communication Conventions:
		// 1 -> XCB_CW_OVERRIDE_REDIRECT;
		// изменение размера;
		// 0 -> XCB_CW_OVERRIDE_REDIRECT.
		case XCB_RESIZE_REQUEST:
			// Повторы не наблюдаются, но и утечки недопустимы.
			if (resize) {
				assert(resize->window == ((xcb_resize_request_event_t*)e)->window);
				free(resize);
			}
			resize = (xcb_resize_request_event_t*)e;
			xcb_change_window_attributes(connection, resize->window,
			                             XCB_CW_OVERRIDE_REDIRECT, &(uint32_t[]){1});
			e = NULL;
			break;
		// TODO XCB spams XCB_GE_GENERIC event after Vulkan window resize
		// https://gitlab.freedesktop.org/mesa/mesa/-/issues/827
		default:
			break;
		}
		if (e)
			free(e);
		e = xcb_poll_for_event(connection);
	}
	if (expose) {
		draw_frame(window_get_ptr(expose->window));
		free(expose);
		flush = true;
	}
	// При вызове до draw_frame() случаются неудачи в vk_present_frame().
	if (resize) {
		on_resize(resize);
		xcb_change_window_attributes(connection, resize->window,
		                             XCB_CW_OVERRIDE_REDIRECT, &(uint32_t[]){0});
		free(resize);
		flush = true;
	}
	if (flush)
		xcb_flush(connection);
	return !xcb_connection_has_error(connection);
}

void window_destroy(struct window *window)
{
	if (window->render->destroy && window->render_ctx)
		window->render->destroy(window->render_ctx);
	if (cursor_context)
		xcb_cursor_context_free(cursor_context);
	if (colormap)
		xcb_free_colormap(connection, colormap);
	xcb_destroy_window(connection, window->window);
}
