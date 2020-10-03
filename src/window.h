/**\file
 * \brief	Интерфейс клиента Wayland/X11.
 *
 *  Выбор платформы осуществляется на этапе трансляции.
 *  Если задан макрос FH_PLATFORM_XCB, используется X11.
 */

#pragma once

#include <assert.h>
#include <stdbool.h>

#ifdef FH_PLATFORM_XCB
#include <xcb/xcb.h>
#else
#include <wayland-client.h>
#endif

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

#ifdef FH_PLATFORM_XCB
	/** Идентификатор окна                  */
	xcb_window_t        	window;
#else
	/** Поверхность окна                    */
	struct wl_surface   	*wl_surface;
	/** Пригодная к отображению поверхность */
	struct xdg_surface  	*xdg_surface;
	/** Роль поверхности - главное окно     */
	struct xdg_toplevel 	*toplevel;
	/** Текущее изображение курсора         */
	struct wl_surface   	*cursor;
#endif
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
#ifdef FH_PLATFORM_XCB
	void (*create)(xcb_connection_t *, xcb_window_t, uint32_t width,
	               uint32_t height, void **render_ctx);
#else
	void (*create)(struct wl_display *, struct wl_surface *, uint32_t width,
	               uint32_t height, void **render_ctx);
#endif
	/** Деструктор               */
	void (*destroy)(void *render_ctx);
	/** Вызывается, когда композитор готов отобразить новый кадр.*/
	bool (*draw_frame)(void *render_ctx);
	/** Изменяет размер окна.    */
	void (*resize)(void *render_ctx, uint32_t width, uint32_t height);
};

/** Интерфейс взаимодействия с устройствами ввода */
struct controller {
	/** Указатель над окном. Выход за пределы сигнализируется отрицательной координатой.
	 * \param cursor_name равен NULL при потере фокуса.
	 * \return true если указатель над элементом управления.
	 */
	bool (*hover)(struct window *window, double x, double y, const char **cursor_name);

	/** Изменилось состояние кнопки указательного устройства.
	 * \param button код из linux/input-event-codes.h (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE)
	 * \param state состояние (0 — отпущена; 1 — нажата).
	 * \return true если событие обработано.
	 */
	bool (*click)(struct window *window, double x, double y,
	              const char **cursor_name, uint32_t button, uint32_t state);

	/** Нажатие на сенсорный экран. */
	void (*touch)(struct window *window, double x, double y);
};

/** Инициализирует сеанс и интерфейсы для связи с сервером. */
static inline bool wp_init(void);

/** Диспетчерезует сообщения сервера, ожидая их появления.
 * \return false в случае ошибки, устанавливает errno (только Wayland).
 */
static inline bool wp_dispatch(void);

/** Завершает соединение с сервером и освобождает связанные ресурсы. */
static inline void wp_stop(void);

/** Создаёт окно и связанные объекты */
bool window_create(struct window *window);

/** Удаляет окно и связанные объекты */
void window_destroy(struct window *window);

static inline void window_geometry(struct window *window)
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
}

#ifdef FH_PLATFORM_XCB

bool xcb_init(void);
bool xcb_dispatch(void);
void xcb_stop(void);
bool wp_init(void) { return xcb_init(); }
bool wp_dispatch(void) { return xcb_dispatch(); }
void wp_stop(void) { xcb_stop(); }

#else

bool wayland_init(void);
bool wayland_dispatch(void);
void wayland_stop(void);
bool wp_init(void) { return wayland_init(); }
bool wp_dispatch(void) {	return wayland_dispatch(); }
void wp_stop(void) { wayland_stop(); }

#endif//#ifdef FH_PLATFORM_XCB
