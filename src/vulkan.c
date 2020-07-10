
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "vulkan.h"

static const char *instance_extensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};

static const char *device_extensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static const struct VkApplicationInfo appinfo = {
	.sType             	 = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	.pApplicationName  	 = "",
	.applicationVersion	 = VK_MAKE_VERSION(0, 1, 0),
	.pEngineName       	 = "",
	.engineVersion     	 = VK_MAKE_VERSION(0, 1, 0),
	.apiVersion        	 = VK_MAKE_VERSION(1, 2, 0),
};

static const struct VkInstanceCreateInfo instinfo = {
	.sType                  	= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	.pApplicationInfo       	= &appinfo,
	.enabledLayerCount      	= 0,
	.ppEnabledLayerNames    	= NULL,
	.enabledExtensionCount  	= sizeof instance_extensions/sizeof*instance_extensions,
	.ppEnabledExtensionNames	= instance_extensions,
};

static const VkAllocationCallbacks	*allocator = NULL;

/** Инстанция для связи с библиотекой Vulkan. */
static VkInstance	instance;

VkResult vk_init(void)
{
	VkResult r = vkCreateInstance(&instinfo, allocator, &instance);
	if (r == VK_SUCCESS)
		printf("Инициализация Vulkan:\n");
	return r;
}

void vk_stop(void)
{
	vkDestroyInstance(instance, allocator);
}


/** Адреса в массивах семейств очередей [команд].
 *
 * Для упрощения инициализации VkSwapchainCreateInfoKHR (\see create_swapchain)
 * и единообразия получения описателей с помощью vkGetDeviceQueue (\see create_device),
 * описатели и индексы семейств очередей хранятся в параллельных массивах.
 */
enum {
	/** ...обработки графики. */
	vk_graphics,
	/** ...вывода графики.    */
	vk_presentation,
	vk_num_queues,
	vk_first_queue = vk_graphics
};

struct vk_context {
	/** Описатель поверхности [окна] графического интерфейса. */
	VkSurfaceKHR    	surface;

	/** Графический сопроцессор.                              */
	VkPhysicalDevice	gpu;

	/** Ассоциированное логическое устройство.                */
	VkDevice        	device;

	/** Семейства очередей                                    */
	VkQueue         	queue[vk_num_queues];
	/** и их индексы.                                         */
	uint32_t        	qi[vk_num_queues];

	/** Последовательность кадров, отображаемых по очереди.   */
	VkSwapchainKHR  	swapchain;
	/** Размер кадра.                                         */
	VkExtent2D      	extent;
	/** Формат элементов изображения.                         */
	VkFormat        	format;
	/** Количество кадров в последовательности.               */
	uint32_t        	count;
	/** Массив видов.                                         */
	VkImageView     	*views;
};

/** Создаёт связанную с Воландом поверхность Вулкан. */
static VkResult create_surface(struct vk_context *vk, struct wl_display *display, struct wl_surface *surface)
{
	const struct VkWaylandSurfaceCreateInfoKHR surfinfo = {
		.sType  	= VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.pNext  	= NULL,
		.flags  	= 0,
		.display	= display,
		.surface	= surface,
	};
	return vkCreateWaylandSurfaceKHR(instance, &surfinfo, allocator, &vk->surface);
}

/** Выбирает подходящий для работы с поверхностью графический процессор  */
/*  и определяет семейства очередей для операций с графикой и её вывода. */
static VkResult select_gpu(struct vk_context *vk)
{
	// Валидные индексы начинаются с 0 и граничных значений быть не должно.
	const uint32_t inv = (uint32_t)-1;
	vk->qi[vk_graphics] = vk->qi[vk_presentation] = inv;
	uint32_t num_dev = 0;
	VkResult r = vkEnumeratePhysicalDevices(instance, &num_dev, NULL);
	printf(" Доступно графических процессоров с поддержкой Vulkan: %u.\n", num_dev);
	if (num_dev) {
		VkPhysicalDevice *devs = calloc(num_dev, sizeof(*devs));
		// TODO Может ли количество физ.устройств увеличиться?
		vkEnumeratePhysicalDevices(instance, &num_dev, devs);
		for (uint32_t d = 0; d < num_dev; ++d) {
			uint32_t num_qf = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &num_qf, NULL);
			printf("  Сопроцессор №%u поддерживает семейств очередей: %u.\n", d+1, num_qf);
			if (!num_qf)
				continue;

			// Перебираем семейства очередей, пока не будут найдены оба требуемых.
			VkQueueFamilyProperties *props = calloc(num_qf, sizeof(*props));
			vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &num_qf, props);
			for (uint32_t i = 0; i < num_qf; ++i) {
				bool gfx_q = props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
				printf("    Графические операции: %s.\n", gfx_q ? "да":"нет");
				if (gfx_q)
					vk->qi[vk_graphics] = i;
				VkBool32 presentation = VK_FALSE;
				// Результат на VK_ERROR_SURFACE_LOST_KHR не проверяем,
				// c presentation VK_FALSE до установки gpu не дойдёт.
				r = vkGetPhysicalDeviceSurfaceSupportKHR(devs[d], i, vk->surface, &presentation);
				printf("    Вывод изображения: %s.\n", presentation ? "да":"нет");
				if (presentation)
					vk->qi[vk_presentation] = i;

				if (gfx_q != inv && presentation != inv) {
					vk->gpu = devs[d];
					break;
				}
			}
			free(props);
		}
		free(devs);
	}
	return r;
}

/** Создаёт связанное с графическим процессором логическое устройство */
/*  и получает описатели для доступа к очередям [команд].             */
static VkResult create_device(struct vk_context *vk)
{
	const float priority = 1.0f;
	const VkDeviceQueueCreateInfo queues[] = {
		{
			.sType           	= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex	= vk->qi[vk_graphics],
			.queueCount      	= 1,
			.pQueuePriorities	= &priority,
		}, {
			.sType           	= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex	= vk->qi[vk_presentation],
			.queueCount      	= 1,
			.pQueuePriorities	= &priority,
		},
	};
	const VkDeviceCreateInfo devinfo = {
		.sType                  	= VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount   	= sizeof queues/sizeof*queues,
		.pQueueCreateInfos      	= queues,
		.enabledLayerCount      	= 0,
		.ppEnabledLayerNames    	= NULL,
		.enabledExtensionCount  	= sizeof device_extensions/sizeof*device_extensions,
		.ppEnabledExtensionNames	= device_extensions,
	};
	vk->device = VK_NULL_HANDLE;
	VkResult r = vkCreateDevice(vk->gpu, &devinfo, allocator, &vk->device);
	if (r == VK_SUCCESS) {
		for (int i = vk_first_queue; i < vk_num_queues; ++i)
			vkGetDeviceQueue(vk->device, vk->qi[i], 0, &vk->queue[i]);
		printf(" Сопроцессор подключён.\n");
	}
	return r;
}

/** Подготавливает массив буферов кадров для построения видеоряда. */
static VkResult create_swapchain(struct vk_context *vk, uint32_t width, uint32_t height)
{
	VkSurfaceCapabilitiesKHR	surfcaps;
	VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->gpu, vk->surface, &surfcaps);
	if (r != VK_SUCCESS)
		return r;
	printf(" Допустимое количество кадров последовательности: %u..%u\n",
	         surfcaps.minImageCount,
	         surfcaps.maxImageCount ? surfcaps.maxImageCount : (uint32_t)-1);

	uint32_t	n_formats = 0, chosen_format = 0;
	r = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->gpu, vk->surface, &n_formats, NULL);
	if (r != VK_SUCCESS)
		return r;
	// Как минимум один формат обязательно поддержан.
	VkSurfaceFormatKHR *formats = calloc(n_formats, sizeof(*formats));
	vkGetPhysicalDeviceSurfaceFormatsKHR(vk->gpu, vk->surface, &n_formats, formats);
	for (uint32_t i = 0; i < n_formats; ++i) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
		    formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			chosen_format = i;
			printf(" Доступен формат VK_FORMAT_B8G8R8A8_SRGB.\n");
			break;
		}
	}
	vk->format = formats[chosen_format].format;
	if (surfcaps.currentExtent.width == 0xFFFFFFFF) {
		vk->extent.width  = width;
		vk->extent.height = height;
	} else {
		vk->extent.width  = surfcaps.currentExtent.width;
		vk->extent.height = surfcaps.currentExtent.height;
	}
	const bool excl = vk->qi[vk_graphics] == vk->qi[vk_presentation];
	const VkSwapchainCreateInfoKHR swch = {
		.sType                	= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface              	= vk->surface,
		.minImageCount        	= surfcaps.minImageCount, // TODO +1?
		.imageFormat          	= vk->format,
		.imageColorSpace      	= formats[chosen_format].colorSpace,
		.imageExtent          	= vk->extent,
		.imageArrayLayers     	= 1,
		.imageUsage           	= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode     	= excl ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount	= excl ? 0 : vk_num_queues,
		.pQueueFamilyIndices  	= &vk->qi[vk_first_queue],
		.preTransform         	= surfcaps.currentTransform,
		.compositeAlpha       	= VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode          	= VK_PRESENT_MODE_FIFO_KHR,
		.clipped              	= VK_TRUE,
		.oldSwapchain         	= VK_NULL_HANDLE,
	};
	r = vkCreateSwapchainKHR(vk->device, &swch, allocator, &vk->swapchain);
	if (r == VK_SUCCESS) {
		vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->count, NULL);
		VkImage *images = calloc(vk->count, sizeof(*images));
		vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->count, images);
		printf(" Создан буфер видеоряда %ux%ux%u.\n",
		        swch.imageExtent.width, swch.imageExtent.height, vk->count);
		vk->views = calloc(vk->count, sizeof(*vk->views));
		for(uint32_t i = 0; i != vk->count; ++i) {
			VkImageViewCreateInfo	viewinfo = {
				.sType          	= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image          	= images[i],
				.viewType       	= VK_IMAGE_VIEW_TYPE_2D,
				.format         	= vk->format,
				.components     	= {
					.r	= VK_COMPONENT_SWIZZLE_IDENTITY,
					.g	= VK_COMPONENT_SWIZZLE_IDENTITY,
					.b	= VK_COMPONENT_SWIZZLE_IDENTITY,
					.a	= VK_COMPONENT_SWIZZLE_IDENTITY,
				},
				.subresourceRange	= {
					.aspectMask  	= VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel	= 0,
					.levelCount  	= 1,
					.baseArrayLayer	= 0,
					.layerCount  	= 1,
				},
			};
			r = vkCreateImageView(vk->device, &viewinfo, allocator, &vk->views[i]);
			if (r == VK_SUCCESS)
				printf("   Проекция кадра №%u.\n", i);
			else
				break;
		}
		free(images);
	}
	free(formats);
	return r;
}

/** Формирует кадр. */
void vk_draw_frame(struct vk_context *vk)
{
	uint32_t image_index;
	vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &image_index);

	VkSwapchainKHR swapchains[] = { vk->swapchain };
	const struct VkPresentInfoKHR prinfo = {
		.sType             	= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount	= 0,
		.pWaitSemaphores   	= NULL,
		.swapchainCount    	= 1,
		.pSwapchains       	= swapchains,
		.pImageIndices     	= &image_index,
		.pResults          	= NULL,
	};
	vkQueuePresentKHR(vk->queue[vk_presentation], &prinfo);
}

/** Удаляет оконную поверхность и связанные структуры. */
void vk_window_destroy(struct vk_context *vk)
{
	while (vk->count--)
		vkDestroyImageView(vk->device, vk->views[vk->count], allocator);
	free(vk->views);
	vkDestroySwapchainKHR(vk->device, vk->swapchain, allocator);
	vkDestroyDevice(vk->device, allocator);
	vkDestroySurfaceKHR(instance, vk->surface, allocator);
	free(vk);
}

struct vk_context*
vk_window_create(struct wl_display *display, struct wl_surface *surface,
                 uint32_t width, uint32_t height)
{
	struct vk_context *vk = calloc(1, sizeof(*vk));
	while (create_surface(vk, display, surface) == VK_SUCCESS) {
		select_gpu(vk);
		create_device(vk);
		create_swapchain(vk, width, height);

		vk_draw_frame(vk);
		return vk;
	}
	vk_window_destroy(vk);
	return NULL;
}

