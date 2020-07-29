
#pragma once

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#ifndef ENABLE_VK_VALIDATION
  #ifndef NDEBUG
    #define ENABLE_VK_VALIDATION 1
  #endif
#elif !ENABLE_VK_VALIDATION
  #undef ENABLE_VK_VALIDATION
#endif

VkResult vk_init(void);
void vk_stop(void);

struct vk_context;

/** Создаёт оконную поверхность и связанные структуры. */
void vk_window_create(struct wl_display *, struct wl_surface *,
                      uint32_t width, uint32_t height, void **object);

void vk_window_destroy(struct vk_context*);

void vk_window_resize(void *vk_context, uint32_t width, uint32_t height);

struct pos2d {
	float	x;
	float	y;
};

struct color {
	float	r;
	float	g;
	float	b;
};

struct vertex2d {
	struct pos2d	pos;
	struct color	color;
};

/** Захватывает очередной кадр видеоряда, при необходимости инициализации */
/** создаёт буфера кадра и команд для его построения.                     */
VkResult vk_acquire_frame(struct vk_context *vk);

/** Подготавливает буфер вершин для заполнения.                           */
VkResult vk_begin_vertex_buffer(struct vk_context *vk, VkDeviceSize size, void **dest);
/** Завершает заполнения буфера вершин для заполнения.                    */
void vk_end_vertex_buffer(struct vk_context *vk);

VkResult vk_begin_render_cmd(struct vk_context *vk);
void vk_cmd_draw_vertices(struct vk_context *vk, uint32_t count, uint32_t first);
VkResult vk_end_render_cmd(struct vk_context *vk);

/** Отображает кадр. */
VkResult vk_present_frame(struct vk_context*);
