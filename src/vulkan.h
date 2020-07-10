
#pragma once

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

VkResult vk_init(void);
void vk_stop(void);

struct vk_context;

struct vk_context* vk_window_create(struct wl_display *, struct wl_surface *, uint32_t width, uint32_t height);
void vk_window_destroy(struct vk_context*);

