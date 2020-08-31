
#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "xdg-shell-client-protocol.h"

#include "wayland_window.h"

/**
 * Ключевой объект для связи с сервером.
 */
static struct wl_display   	*display;

/**
 * Реестр глобальных объектов, доступных клиентам.
 * Сервер уведомляет подписчика об изменениях.
 */
static struct wl_registry  	*registry;

/**
 * Композитор организует отображение поверхностей и регионов на дисплее.
 */
static struct wl_compositor	*compositor;

/**
 * Обеспечивает поддержку общей для клиента и сервера памяти.
 */
static struct wl_shm       	*shared_mem;

/**
 * Сопоставляет wl_surfaces с окнами, отображаемыми в рабочей среде.
 */
static struct xdg_wm_base  	*wm_base;

/**
 * Абстракция рабочего места, предоставляет устройства ввода.
 */
static struct wl_seat      	*seat;

const char                 	*seat_name;

/**
 * Указательное устройство (мышь).
 */
static struct wl_pointer   	*pointer;


/**
 * Описатель импортируемого интерфейса.
 * Сопоставляет библиотечный интерфейс с указателем на объект.
 */
struct interface_descriptor {
	const struct wl_interface	*interface;
	int                      	version;
	void                     	**object;
};

/**
 * Когда реестр информирует о наличии объекта, обработчик ищет по имени
 * соответствующий интерфейс и связывает его с указателем на имплементацию.
 */
static void on_global_add(void *p, struct wl_registry *registry,
                          uint32_t name, const char *interface, uint32_t version)
{
	const struct interface_descriptor *globals = p;
#ifdef DEBUGLOG
	printf("%s версия %d, идентификатор %d\n", interface, version, name);
#endif
	for (const struct interface_descriptor *i = globals; i->interface; ++i) {
		if (!strcmp(interface, i->interface->name)) {
			int vers = i->version < version ? i->version : version;
			*i->object = wl_registry_bind(registry, name, i->interface, vers);
			printf("Получен интерфейс %s.\n", interface);
		}
	}
}

static void on_global_remove(void *p, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global       	= on_global_add,
	.global_remove	= on_global_remove,
};


static void on_wm_base_ping(void *p, struct xdg_wm_base *wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = &on_wm_base_ping,
};


struct seat_ctx {
	struct window	*pointer_focus;
	uint32_t     	pointer_serial;
	uint32_t     	pointer_time;
	wl_fixed_t   	pointer_x;
	wl_fixed_t   	pointer_y;
	uint32_t     	pointer_button;
	uint32_t     	pointer_state;
	wl_fixed_t   	pointer_axis_h;
	wl_fixed_t   	pointer_axis_v;
	uint32_t     	pointer_axis_source;
	int32_t      	pointer_discrete;
	enum seat_pointer_event {
		pointer_enter        	= 1 << 0,
		pointer_leave        	= 1 << 1,
		pointer_motion       	= 1 << 2,
		pointer_button       	= 1 << 3,
		pointer_axis_h       	= 1 << 4,
		pointer_axis_v       	= 1 << 5,
		pointer_axis_source  	= 1 << 6,
		pointer_axis_stop    	= 1 << 8,
		pointer_axis_discrete	= 1 << 10,
	}            	pointer_event;
};

static struct seat_ctx seat_ctx;

/** Указатель появился над поверхностью. */
static void on_pointer_enter(void *p, struct wl_pointer *pointer, uint32_t serial,
                             struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	struct seat_ctx *inp = p;
	assert(!inp->pointer_focus);
	// До события leave получаемые данные соответствуют данному окну.
	inp->pointer_focus  = wl_surface_get_user_data(surface);
	inp->pointer_serial = serial;
	inp->pointer_x = x;
	inp->pointer_y = y;
	inp->pointer_event |= pointer_enter;
}

static void on_pointer_leave(void *p, struct wl_pointer *pointer, uint32_t serial,
                             struct wl_surface *surface)
{
	struct seat_ctx *inp = p;
	struct window *window = wl_surface_get_user_data(surface);
	assert(window == inp->pointer_focus);
	inp->pointer_focus  = NULL;
	inp->pointer_serial = serial;
	inp->pointer_event |= pointer_leave;
}

static void on_pointer_motion(void *p, struct wl_pointer *pointer, uint32_t time,
                              wl_fixed_t x, wl_fixed_t y)
{
	struct seat_ctx *inp = p;
	inp->pointer_time = time;
	inp->pointer_x = x;
	inp->pointer_y = y;
	inp->pointer_event |= pointer_motion;
}

static void on_pointer_button(void *p, struct wl_pointer *pointer, uint32_t serial,
                              uint32_t time, uint32_t button, uint32_t state)
{
	struct seat_ctx *inp = p;
	inp->pointer_serial = serial;
	inp->pointer_time   = time;
	inp->pointer_button = button;
	inp->pointer_state  = state;
	inp->pointer_event |= pointer_button;
}

static void on_pointer_axis(void *p, struct wl_pointer *pointer, uint32_t time,
                            uint32_t axis, wl_fixed_t value)
{
	struct seat_ctx *inp = p;
	inp->pointer_time = time;
	switch (axis) {
	case 0:
		inp->pointer_axis_h = value;
		inp->pointer_event |= pointer_axis_h;
		break;
	case 1:
		inp->pointer_axis_v = value;
		inp->pointer_event |= pointer_axis_v;
		break;
	default:
		assert(0);
		break;
	}
}

static void on_pointer_axis_source(void *p, struct wl_pointer *pointer, uint32_t axis_source)
{
	struct seat_ctx *inp = p;
	inp->pointer_axis_source = axis_source;
	inp->pointer_event |= pointer_axis_source;
}

static void on_pointer_axis_stop(void *p, struct wl_pointer *pointer,
                                 uint32_t time, uint32_t axis)
{
	struct seat_ctx *inp = p;
	inp->pointer_time = time;
	// TODO различать оси?
	inp->pointer_event |= pointer_axis_stop;
}

static void on_pointer_axis_discrete(void *p, struct wl_pointer *pointer,
                                     uint32_t axis, int32_t discrete)
{
	struct seat_ctx *inp = p;
	inp->pointer_discrete = discrete;
	// TODO различать оси?
	inp->pointer_event |= pointer_axis_discrete;
}

static uint32_t resize_edge(struct window *window, int x, int y)
{
	uint32_t resize = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
	if (x < window->border) {
		resize = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
	} else if (x >= window->width - window->border) {
		resize = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
	}
	if (y < window->border) {
		switch (resize) {
		default:
		case XDG_TOPLEVEL_RESIZE_EDGE_NONE:
			resize = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
			break;
		case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
			resize = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
			break;
		case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
			resize = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
			break;
		}
	} else if (y >= window->height - window->border) {
		switch (resize) {
		default:
		case XDG_TOPLEVEL_RESIZE_EDGE_NONE:
			resize = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
			break;
		case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
			resize = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
			break;
		case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
			resize = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
			break;
		}
	}
	return resize;
}

/** Данные от устройства переданы в количестве необходимом для обработки. */
static void on_pointer_frame(void *p, struct wl_pointer *pointer)
{
	struct seat_ctx *inp = p;
	struct window *window = inp->pointer_focus;
	const int x = wl_fixed_to_int(inp->pointer_x);
	const int y = wl_fixed_to_int(inp->pointer_y);

	// По щелчку у кромки изменяем размер окна, если позволено.
	// Если щелчёк не обработан, двигаем окно.
	if (pointer_button & inp->pointer_event) {
		uint32_t resize = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
		if (window->render->resize) {
			resize = resize_edge(window, x, y);
		}
		if (resize != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
			xdg_toplevel_resize(window->toplevel, seat, inp->pointer_serial, resize);
		} else {
			xdg_toplevel_move(window->toplevel, seat, inp->pointer_serial);
		}
	}

	inp->pointer_event = 0;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter        	= on_pointer_enter,
	.leave        	= on_pointer_leave,
	.motion       	= on_pointer_motion,
	.button       	= on_pointer_button,
	.axis         	= on_pointer_axis,
	.frame        	= on_pointer_frame,
	.axis_source  	= on_pointer_axis_source,
	.axis_stop    	= on_pointer_axis_stop,
	.axis_discrete	= on_pointer_axis_discrete,
};

static void seat_capabilities(void *p, struct wl_seat *seat, uint32_t capabilities)
{
	struct seat_ctx *inp = p;
	if (capabilities & WL_SEAT_CAPABILITY_POINTER && !pointer) {
		printf("Подключено указательное устройство.\n");
		pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, inp);
	} else if (pointer && !(capabilities & WL_SEAT_CAPABILITY_POINTER)) {
		printf("Отключено указательное устройство.\n");
		wl_pointer_destroy(pointer);
		pointer = NULL;
	}
}

static void set_seat_name(void *p, struct wl_seat *s, const char *name)
{
	assert(s == seat);
	seat_name = strdup(name);
	printf("Пульт управления: %s\n", seat_name);
}

const static struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name         = set_seat_name,
};


static const struct interface_descriptor globals[] = {
	{ &wl_compositor_interface,	4,	(void**)&compositor },
	{ &wl_seat_interface,      	7,	(void**)&seat       },
	{ &wl_shm_interface,       	1,	(void**)&shared_mem },
	{ &xdg_wm_base_interface,  	1,	(void**)&wm_base    },
	{                          	  	                    }
};

bool wayland_init(void)
{
	display = wl_display_connect(NULL);
	if (display == NULL)
		return false;
	registry = wl_display_get_registry(display);
	if (registry == NULL)
		return false;
	wl_registry_add_listener(registry, &registry_listener, (void*)globals);
	wl_display_roundtrip(display);
	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
	wl_seat_add_listener(seat, &seat_listener, &seat_ctx);
	return compositor && shared_mem && wm_base;
}

void wayland_stop(void)
{
	// TODO on_global_remove()
	if (seat) {
		wl_seat_destroy(seat);
		free((char*)seat_name);
	}
	if (display) {
		wl_display_disconnect(display);
		display = NULL;
	}

}

/** Резервирует в ОЗУ блок требуемого размера и возвращает его описатель. */
static int shm_alloc(off_t size)
{
	int fd = memfd_create("wl", MFD_CLOEXEC|MFD_ALLOW_SEALING);
	if (fd != -1) {
		if (ftruncate(fd, size) == 0)
			fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL);
		else {
			close(fd);
			fd = -1;
		}
	}
	return fd;
}

/** Вызывается композитором когда буфер больше не нужен. */
static void on_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
	.release = on_buffer_release,
};

static struct wl_buffer *shm_buffer_create(struct window *window)
{
	int size = sizeof(uint32_t) * window->width  * window->height;
	int fd = shm_alloc(size);
	struct wl_buffer *buffer = NULL;
	if (fd != -1) {
		struct wl_shm_pool *pool = wl_shm_create_pool(shared_mem, fd, size);
		close(fd);
		buffer = wl_shm_pool_create_buffer(pool, 0, window->width, window->height,
				sizeof(uint32_t) * window->width, WL_SHM_FORMAT_XRGB8888);
		wl_shm_pool_destroy(pool);
		wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
	}
	return buffer;
}


/** Вызывается композитором, когда пришло время отрисовать окно. */
static void draw_frame(void *p, struct wl_callback *cb, uint32_t time)
{
	if (cb)
		wl_callback_destroy(cb);

	struct window *w = p;
	assert(w->render->draw_frame);
	w->render->draw_frame(w->render_ctx);

	// TODO Vulkan неявно вызывает нижеследующее в vkQueuePresentKHR();
	static const struct wl_callback_listener nf = { draw_frame };
	wl_callback_add_listener(wl_surface_frame(w->wl_surface), &nf, w);
	wl_surface_commit(w->wl_surface);
}


static void on_xdg_surface_configure(void *p, struct xdg_surface *surface, uint32_t serial)
{
	printf("on_xdg_surface_configure\n");
	struct window *window = p;
	xdg_surface_ack_configure(surface, serial);
	if (window->pending_resize) {
		assert(window->render->resize);
		window->render->resize(window->render_ctx, window->width, window->height);
		window->pending_resize = false;
	} else if (window->pending_configure) {
		// выполняет wl_surface_commit()
		draw_frame(window, NULL, 0);
		window->pending_configure = false;
	}
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = &on_xdg_surface_configure,
};

static void on_toplevel_configure(void *p, struct xdg_toplevel *toplevel,
                                  int32_t width, int32_t height, struct wl_array *states)
{
	struct window *window = p;
	printf("Размер окна: %u*%u.\n", width, height);

	// Нулевым параметром композитор указывает, что следует установить требуемый
	// клиенту размер окна. Сигнализируем флажком для on_xdg_surface_configure().
	// Иначе изменяем размер окна, если требуется.
	if (!width || !height) {
		window->pending_configure = true;
	} else {
		// Если требуется сохранить соотношение сторон:
		// вычисляем размер неподвижной стороны из изменяющейся;
		// при изменении сразу 2-х сторон вычисляем некое среднее,
		// поскольку в данном случае пользователь может один размер
		// уменьшать, а второй увеличивать — и не ясно, что он хочет.
		if (window->aspect_ratio) {
			if (window->width != width && window->height != height) {
				int w1 = height * window->aspect_ratio;
				height = (height + width / window->aspect_ratio) / 2;
				width  = (width + w1) / 2;
			} else if (window->width  != width) {
				height = width / window->aspect_ratio;
			} else if (window->height != height) {
				width = height * window->aspect_ratio;
			}
			if ((window->height != height || window->width != width)
			  && height >= 2 * window->border && width >= 2 * window->border) {
				window->height = height;
				window->width  = width;
				window->pending_resize = true;
			}
		} else {
			if (window->width != width && width >= 2 * window->border) {
				window->width  = width;
				window->pending_resize = true;
			}
			if (window->height != height && height >= 2 * window->border) {
				window->height = height;
				window->pending_resize = true;
			}
		}
	}
}

static void on_toplevel_close(void *p, struct xdg_toplevel *toplevel)
{
	struct window *window = p;
	printf("Закрытие.\n");
	window->close = true;
}

const static struct xdg_toplevel_listener toplevel_listener = {
	.configure = on_toplevel_configure,
	.close     = on_toplevel_close,
};


void window_create(struct window *window)
{
	assert(!window->height || !window->width || !window->aspect_ratio || window->width == window->aspect_ratio * window->height);

	if (window->constant_aspect_ratio)
		window->aspect_ratio = window->width / (double)window->height;
	if (!window->height && window->aspect_ratio)
		window->height = window->width / window->aspect_ratio + 0.5;
	if (!window->width && window->aspect_ratio)
		window->width = window->height * window->aspect_ratio + 0.5;

	assert(2 * window->border <= window->width);
	assert(2 * window->border <= window->height);

	window->wl_surface = wl_compositor_create_surface(compositor);
	wl_surface_set_user_data(window->wl_surface, window);

	window->xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, window->wl_surface);
	xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
	window->toplevel = xdg_surface_get_toplevel(window->xdg_surface);
	xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
	xdg_toplevel_set_title(window->toplevel, window->title);

	assert(window->render);
	assert(window->render->create);
	window->render->create(display, window->wl_surface, window->width,
	                       window->height, &window->render_ctx);
	assert(window->render_ctx);

	wl_surface_commit(window->wl_surface);
}

void window_dispatch(struct window *window)
{
	while (1) {
		// TODO поскольку вызов блокирующий, до проверки window->close может
		// не дойти. При этом (пока) draw_frame() продолжает отрисовку,
		// планируя дальнейшие вызовы независимо от window->close.
		if (wl_display_dispatch(display) == -1)
			printf("Errno: %i\n", errno);
		if (window->close)
			break;
	}
}

void window_destroy(struct window *window)
{
	if (window->render->destroy)
		window->render->destroy(window->render_ctx);

	xdg_toplevel_destroy(window->toplevel);
	xdg_surface_destroy(window->xdg_surface);
	wl_surface_destroy(window->wl_surface);
}
