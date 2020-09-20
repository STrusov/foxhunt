/**\file
 * \brief	Интерфейс клиента Wayland.
 */

#pragma once

#include <stdbool.h>
#include <wayland-client.h>

/** Описывает окно. */
struct window {
	/** таблица функций интерфейса управления   */
	const struct controller	*ctrl;
	/** Таблица функций построителя изображений */
	const struct render 	*render;
	/** Заголовок окна                      */
	const char          	*title;
	/** Ширина окна                         */
	int                 	width;
	/** Высота окна                         */
	int                 	height;
	/** Ширина рамки, захватываемой для изменения размеров */
	int                 	border;
	/** При изменении размеров соотношение сторон сохраняется */
	bool                	constant_aspect_ratio;
	/** Соотношение сторон, вычисляется если требуется его постоянство */
	double               	aspect_ratio;

	/** Данные построителя изображений      */
	void                	*render_ctx;
	/** Поверхность окна                    */
	struct wl_surface   	*wl_surface;
	/** Пригодная к отображению поверхность */
	struct xdg_surface  	*xdg_surface;
	/** Роль поверхности - главное окно     */
	struct xdg_toplevel 	*toplevel;
	/** Текущее изображение курсора         */
	struct wl_surface   	*cursor;
	/** Имя текущего курсора                */
	const char          	*cursor_name;

	bool                	pending_configure;
	bool                	pending_resize;
	/** Окно требует закрытия               */
	bool                	close;
};

/** Интерфейс для отрисовки окна */
struct render {
	/** Конструктор              */
	void (*create)(struct wl_display *, struct wl_surface *, uint32_t width,
	               uint32_t height, void **render_ctx);
	/** Деструктор               */
	void (*destroy)(void *render_ctx);
	/** Вызывается, когда композитор готов отобразить новый кадр.*/
	bool (*draw_frame)(void *render_ctx);
	/** Изменяет размер окна.    */
	void (*resize)(void *render_ctx, uint32_t width, uint32_t height);
};

/** Интерфейс взаимодействия с устройствами ввода */
struct controller {
	/** Указатель над окном. Выход за пределы сигнализируется отрицательной координатой. */
	void (*hover)(struct window *window, double x, double y, const char **cursor_name);

	/** Изменилось состояние кнопки указательного устройства.
	 * \param button код из linux/input-event-codes.h (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE)
	 * \param state состояние (0 — отпущена; 1 — нажата).
	 */
	bool (*click)(struct window *window, double x, double y,
	              const char **cursor_name, uint32_t button, uint32_t state);
};

/** Инициализирует сеанс и интерфейсы для связи с сервером. */
bool wayland_init(void);

/** Завершает соединение с сервером и освобождает связанные ресурсы. */
void wayland_stop(void);

/** Создаёт окно и связанные объекты */
void window_create(struct window *window);

/** Удаляет окно и связанные объекты */
void window_destroy(struct window *window);

void window_dispatch(struct window *window);
