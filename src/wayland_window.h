
#pragma once

#include <stdbool.h>
#include <wayland-client.h>

struct window {
	/** Таблица функций построителя изображений */
	const struct render 	*render;
	/** Заголовок окна                      */
	const char          	*title;
	/** Ширина окна                         */
	int                 	width;
	/** Высота окна                         */
	int                 	height;

	/** Данные построителя изображений      */
	void                	*render_ctx;
	/** Поверхность окна                    */
	struct wl_surface   	*wl_surface;
	/** Пригодная к отображению поверхность */
	struct xdg_surface  	*xdg_surface;
	/** Роль поверхности - главное окно     */
	struct xdg_toplevel 	*toplevel;
};

/** Интерфейс для отрисовки окна */
struct render {
	/** Конструктор              */
	void (*create)(struct wl_display *, struct wl_surface *, uint32_t width,
	               uint32_t height, void **render_ctx);
	/** Вызывается, когда композитор готов отобразить новый кадр.*/
	bool (*draw_frame)(void *render_ctx);
};

/** Инициализирует сеанс и интерфейсы для связи с сервером. */
bool wayland_init(void);

/** Завершает соединение с сервером и освобождает связанные ресурсы. */
void wayland_stop(void);

/** Создаёт окно и связанные объекты */
void window_create(struct window *window);

void window_dispatch(void);
