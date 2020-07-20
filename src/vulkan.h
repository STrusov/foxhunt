
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

/** Формирует кадр. */
VkResult vk_draw_frame(struct vk_context*);
