
#pragma once

#ifdef FH_PLATFORM_XCB
  #define VK_USE_PLATFORM_XCB_KHR
#else
  #define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include <vulkan/vulkan.h>

/** Активирует проверки параметров Вулкан. */
#ifndef FH_VK_ENABLE_VALIDATION
  #ifndef NDEBUG
    #define FH_VK_ENABLE_VALIDATION 1
  #endif
#elif !FH_VK_ENABLE_VALIDATION
  #undef FH_VK_ENABLE_VALIDATION
#endif

#include "draw.h"

VkResult vk_init(void);
void vk_stop(void);

struct vk_context;

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
typedef struct wl_display	window_server;
typedef struct wl_surface*	window_surface;
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
typedef xcb_connection_t	window_server;
typedef xcb_window_t    	window_surface;
#endif

/** Создаёт оконную поверхность и связанные структуры. */
void vk_window_create(window_server*, window_surface,
                      uint32_t width, uint32_t height, void **object);

void vk_window_destroy(void *vk_context);

void vk_window_resize(void *vk_context, uint32_t width, uint32_t height);


/// Редуцированная матрица трансформации \see shader.vert
struct transform {
	struct vec4 	scale;
	struct vec4 	translate;
};


/** Захватывает очередной кадр видеоряда, при необходимости инициализации */
/** создаёт буфера кадра и команд для его построения.                     */
VkResult vk_acquire_frame(struct vk_context *vk, int64_t timeout);

/** Подготавливает буфер вершин для заполнения.                           */
VkResult vk_begin_vertex_buffer(struct vk_context *vk, VkDeviceSize size, struct vertex **dest);
/** Завершает заполнение буфера вершин.                                   */
void vk_end_vertex_buffer(struct vk_context *vk);

/** Подготавливает буфер индексов для заполнения.                         */
VkResult vk_begin_index_buffer(struct vk_context *vk, VkDeviceSize size, vert_index **dest);
/** Завершает заполнение буфера индексов.                                 */
void vk_end_index_buffer(struct vk_context *vk);

VkResult vk_begin_render_cmd(struct vk_context *vk);
void vk_cmd_push_transform(struct vk_context *vk, const struct transform *tf);
void vk_cmd_draw_vertices(struct vk_context *vk, uint32_t count, uint32_t first);
void vk_cmd_draw_indexed(struct vk_context *vk, uint32_t count);
VkResult vk_end_render_cmd(struct vk_context *vk);

/** Отображает кадр. */
VkResult vk_present_frame(struct vk_context*);
