
#define _GNU_SOURCE

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


static const struct interface_descriptor globals[] = {
	{ &wl_compositor_interface,	4,	(void**)&compositor },
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
	return compositor && shared_mem && wm_base;
}

void wayland_stop(void)
{
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


static void on_xdg_surface_configure(void *p, struct xdg_surface *surface, uint32_t serial)
{
	struct window *window = p;
	struct wl_buffer *buffer = shm_buffer_create(window);

	xdg_surface_ack_configure(surface, serial);
	wl_surface_attach(window->wl_surface, buffer, 0, 0);
	wl_surface_commit(window->wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = &on_xdg_surface_configure,
};

void window_create(struct window *window)
{
	window->wl_surface = wl_compositor_create_surface(compositor);
	window->xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, window->wl_surface);
	xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
	window->toplevel = xdg_surface_get_toplevel(window->xdg_surface);
	xdg_toplevel_set_title(window->toplevel, window->title);
	wl_surface_commit(window->wl_surface);
}

void window_dispatch(void)
{
	while (wl_display_dispatch(display) != -1) {
	}
}

