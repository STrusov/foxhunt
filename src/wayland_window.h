
#pragma once

#include <stdbool.h>
#include <wayland-client.h>

struct window {
	/** Заголовок окна                      */
	const char          	*title;
	/** Ширина окна                         */
	int                 	width;
	/** Высота окна                         */
	int                 	height;

	/** Поверхность окна                    */
	struct wl_surface   	*wl_surface;
	/** Пригодная к отображению поверхность */
	struct xdg_surface  	*xdg_surface;
	/** Роль поверхности - главное окно     */
	struct xdg_toplevel 	*toplevel;
};

/** Инициализирует сеанс и интерфейсы для связи с сервером. */
bool wayland_init(void);

/** Завершает соединение с сервером и освобождает связанные ресурсы. */
void wayland_stop(void);

/** Создаёт окно и связанные объекты */
void window_create(struct window *window);

void window_dispatch(void);
