
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "vulkan.h"

#ifdef FH_VK_ENABLE_VALIDATION
static const char *validation_layers[] = {
	"VK_LAYER_KHRONOS_validation",
};
#endif

static const char *instance_extensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
};

#if !defined(FH_VK_SWAPCHAIN_LAZY_FREE)
#ifdef VK_USE_PLATFORM_XCB_KHR
#define FH_VK_SWAPCHAIN_LAZY_FREE 0
#else
#define FH_VK_SWAPCHAIN_LAZY_FREE 1
#endif
#endif

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
#ifdef FH_VK_ENABLE_VALIDATION
	.enabledLayerCount      	= sizeof validation_layers/sizeof*validation_layers,
	.ppEnabledLayerNames    	= validation_layers,
#else
	.enabledLayerCount      	= 0,
	.ppEnabledLayerNames    	= NULL,
#endif
	.enabledExtensionCount  	= sizeof instance_extensions/sizeof*instance_extensions,
	.ppEnabledExtensionNames	= instance_extensions,
};

static const VkAllocationCallbacks	*allocator = NULL;

/** Инстанция для связи с библиотекой Vulkan. */
static VkInstance	instance;

VkResult vk_init(void)
{
#ifdef FH_VK_ENABLE_VALIDATION
	struct VkInstanceCreateInfo ii = instinfo;
	VkResult r = vkCreateInstance(&ii, allocator, &instance);
	if (r == VK_SUCCESS) {
		printf("Активна прослойка %s.\n", validation_layers[0]);
	} else if (r == VK_ERROR_LAYER_NOT_PRESENT) {
		printf("Отсутствует валидатор Vulkan %s.\n", validation_layers[0]);
		ii.enabledLayerCount = 0;
		r = vkCreateInstance(&ii, allocator, &instance);
	}

#else
	VkResult r = vkCreateInstance(&instinfo, allocator, &instance);
#endif
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
 * При добавлении новых типов очередей важен порядок, для исключения
 * дубликатов при вызове vkCreateDevice (\see create_device).
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
	/** Устаревшая последовательность, ждущая удаления.       */
	VkSwapchainKHR  	old_swapchain;
	/** Размер кадра.                                         */
	VkExtent2D      	extent;
	/** Формат элементов изображения.                         */
	VkSurfaceFormatKHR	format;

	VkSurfaceTransformFlagBitsKHR	transform;
	VkCompositeAlphaFlagsKHR     	supported_alpha;
	uint32_t        	min_count;
	/** Количество кадров в последовательности И номер резервного */
	uint32_t        	count;
	/** Индекс текущего кадра.                                */
	uint32_t        	active;
	/** Последовательность кадров + 1 резерв для old_swapchain.   */
	struct vk_frame 	*frame;
	/** Текущий выбранный из набора (элемент pool).               */
	uint32_t        	pool_current;

	/** Представляет коллекцию привязок, шагов и зависимостей между ними. */
	VkRenderPass    	render_pass;
	/** Хранилище команд для графического процессора          */
	VkCommandPool   	command_pool;

	/** Модули ретушёров                                      */
	VkShaderModule  	shader[2];
	/** Графические конвейеры                                 */
	VkPipeline      	graphics_pipeline;
	VkPipeline      	base_pipeline;
	VkPipeline      	old_pipeline;
	/** и описатель их топологии                              */
	VkPipelineLayout	pipeline_layout;
};

struct vk_buffer {
	VkBuffer        buf;
	VkDeviceMemory  mem;
	VkDeviceSize    size;
};

/** Буфер кадра, проекция и команды построения изображения. */
struct vk_frame {
	VkImage         	img;
	VkFramebuffer   	fb;
	VkSemaphore     	pool;	///< не обязательно относится к данному кадру \see vk_acquire_frame().
	VkSemaphore     	acquired;
	VkSemaphore     	rendered;
	VkImageView     	view;
	VkCommandBuffer 	cmd;
	VkFence         	pending;	///< готовность буфера команд.
	struct vk_buffer	vert;
	struct vk_buffer	indx;
};

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
/** Создаёт связанную с Воландом поверхность Вулкан. */
static VkResult surface_create(VkSurfaceKHR *surface, struct wl_display *display, struct wl_surface *window)
{
	const struct VkWaylandSurfaceCreateInfoKHR surfinfo = {
		.sType  	= VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
		.pNext  	= NULL,
		.flags  	= 0,
		.display	= display,
		.surface	= window,
	};
	return vkCreateWaylandSurfaceKHR(instance, &surfinfo, allocator, surface);
}
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
/** Создаёт связанную с X11 (XCB) поверхность Вулкан. */
static VkResult surface_create(VkSurfaceKHR *surface, xcb_connection_t* connection, xcb_window_t window)
{
	const struct VkXcbSurfaceCreateInfoKHR surfinfo = {
		.sType     	= VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.pNext     	= NULL,
		.flags     	= 0,
		.connection	= connection,
		.window    	= window,
	};
	return vkCreateXcbSurfaceKHR(instance, &surfinfo, allocator, surface);
}
#endif

static const char *gpu_type[] = {
	[VK_PHYSICAL_DEVICE_TYPE_OTHER]          = "",
	[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU] = "Интегрированный",
	[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]   = "Дискретный",
	[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]    = "Виртуальный",
	[VK_PHYSICAL_DEVICE_TYPE_CPU]            = "Программный",
};

static void print_gpu_properties(VkPhysicalDevice gpu)
{
	struct VkPhysicalDeviceProperties gp;
	vkGetPhysicalDeviceProperties(gpu, &gp);
	printf(" %s процессор Vulkan %u.%u.%u %s [%x:%x] v%x.\n",
	       gpu_type[gp.deviceType], VK_VERSION_MAJOR(gp.apiVersion),
	       VK_VERSION_MINOR(gp.apiVersion), VK_VERSION_PATCH(gp.apiVersion),
	       gp.deviceName, gp.vendorID, gp.deviceID, gp.driverVersion);
}

/** Выбирает подходящий для работы с поверхностью графический процессор  */
/*  и определяет семейства очередей для операций с графикой и её вывода. */
static VkResult select_gpu(struct vk_context *vk)
{
	// Валидные индексы начинаются с 0 и граничных значений быть не должно.
	const uint32_t inv = (uint32_t)-1;
	for (int i = vk_first_queue; i < vk_num_queues; ++i)
		vk->qi[i] = inv;
	uint32_t num_dev = 0;
	VkResult r = vkEnumeratePhysicalDevices(instance, &num_dev, NULL);
	printf(" Доступно графических процессоров с поддержкой Vulkan: %u.\n", num_dev);
	if (num_dev) {
		VkPhysicalDevice *devs = calloc(num_dev, sizeof(*devs));
		// TODO Может ли количество физ.устройств увеличиться?
		vkEnumeratePhysicalDevices(instance, &num_dev, devs);
		for (uint32_t d = 0; d < num_dev; ++d) {
			uint32_t num_qf = 0;
			print_gpu_properties(devs[d]);
			vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &num_qf, NULL);
			printf("  Поддерживает семейств очередей: %u.\n", num_qf);
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
					// TODO в случае Intel+Nvidia проверка 2-го устройства
					// приводит к краху vkGetPhysicalDeviceSurfaceSupportKHR
					// в /lib64/libnvidia-glcore.so.450.66.
					// Используем первый подходящий.
					d = num_dev;
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
	// Спецификация Вулкан требует:
	// каждый элемент массива описаний очередей должен быть уникален.
	// https://www.khronos.org/registry/vulkan/specs/1.0-extensions/html/vkspec.html#VUID-VkDeviceCreateInfo-queueFamilyIndex-00372
	uint32_t num_queues = 0;
	struct VkDeviceQueueCreateInfo queues[vk_num_queues] = {};
	for (int i = vk_first_queue; i < vk_num_queues; ++i) {
		for (int j = 0; j < i; ++j)
			if (queues[j].queueFamilyIndex == vk->qi[i])
				goto next_queue;
		queues[num_queues].sType           	= VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queues[num_queues].queueFamilyIndex = vk->qi[i];
		queues[num_queues].queueCount      	= 1;
		queues[num_queues].pQueuePriorities	= &priority;
		++num_queues;
next_queue:
		continue;
	}
	const struct VkDeviceCreateInfo devinfo = {
		.sType                  	= VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount   	= num_queues,
		.pQueueCreateInfos      	= queues,
		.enabledLayerCount      	= 0,
		.ppEnabledLayerNames    	= NULL,
		.enabledExtensionCount  	= sizeof device_extensions/sizeof*device_extensions,
		.ppEnabledExtensionNames	= device_extensions,
	};
	VkResult r = vkCreateDevice(vk->gpu, &devinfo, allocator, &vk->device);
	if (r == VK_SUCCESS) {
		for (int i = vk_first_queue; i < vk_num_queues; ++i)
			vkGetDeviceQueue(vk->device, vk->qi[i], 0, &vk->queue[i]);
		printf(" Сопроцессор подключён.\n");
	}
	return r;
}

/** Простой семафор. */
static const struct VkSemaphoreCreateInfo ssci = {
	.sType	= VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
};

static VkResult surface_characteristics(struct vk_context *vk, uint32_t width, uint32_t height)
{
	VkSurfaceCapabilitiesKHR	surfcaps;
	VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->gpu, vk->surface, &surfcaps);
	if (r != VK_SUCCESS)
		return r;
	// В Wayland 0xFFFFFFFF, а в XCB соответствует размерам окна.
	// Оба варианта подходят для инициализации vk->extent в create_swapchain().
	assert(surfcaps.currentExtent.width == 0xFFFFFFFF || (surfcaps.currentExtent.width == width && surfcaps.currentExtent.height == height));
	vk->transform = surfcaps.currentTransform;
	vk->min_count = surfcaps.minImageCount;  // TODO +1?
	printf(" Допустимое количество кадров последовательности: %u..%u\n",
	         surfcaps.minImageCount,
	         surfcaps.maxImageCount ? surfcaps.maxImageCount : (uint32_t)-1);
	vk->supported_alpha = surfcaps.supportedCompositeAlpha;
	printf(" Поддерживаются наложения: %#x\n", surfcaps.supportedCompositeAlpha);

	uint32_t	n_formats = 0, chosen_format = 0;
	r = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->gpu, vk->surface, &n_formats, NULL);
	if (r != VK_SUCCESS)
		return r;
	// Как минимум один формат обязательно поддержан.
	VkSurfaceFormatKHR *formats = calloc(n_formats, sizeof(*formats));
	if (!formats)
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	vkGetPhysicalDeviceSurfaceFormatsKHR(vk->gpu, vk->surface, &n_formats, formats);
	for (uint32_t i = 0; i < n_formats; ++i) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
		    formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			chosen_format = i;
			printf(" Доступен формат VK_FORMAT_B8G8R8A8_SRGB.\n");
			break;
		}
	}
	vk->format = formats[chosen_format];
	free(formats);
	return r;
}

/** Подготавливает описатель видеоряда. */
static VkResult create_swapchain(struct vk_context *vk, uint32_t width, uint32_t height)
{
	VkResult r;
	if (!vk->min_count) {
		r = surface_characteristics(vk, width, height);
		if (r != VK_SUCCESS)
			return r;
	}
	vk->extent.width  = width;
	vk->extent.height = height;
	const bool excl = vk->qi[vk_graphics] == vk->qi[vk_presentation];
	assert(!vk->old_swapchain);
	vk->old_swapchain = vk->swapchain;
	const VkSwapchainCreateInfoKHR swch = {
		.sType                	= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface              	= vk->surface,
		.minImageCount        	= vk->min_count,
		.imageFormat          	= vk->format.format,
		.imageColorSpace      	= vk->format.colorSpace,
		.imageExtent          	= vk->extent,
		.imageArrayLayers     	= 1,
		.imageUsage           	= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode     	= excl ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
		.queueFamilyIndexCount	= excl ? 0 : vk_num_queues,
		.pQueueFamilyIndices  	= &vk->qi[vk_first_queue],
		.preTransform         	= vk->transform,
		// TODO cпецификация гарантирует поддержку минимум одного типа, но
		// не определяет его. Наивно предполагаем возможность непрозрачного окна.
		.compositeAlpha       	= vk->supported_alpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
		                        ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
		                        : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode          	= VK_PRESENT_MODE_FIFO_KHR,
		.clipped              	= VK_TRUE,
		.oldSwapchain         	= vk->old_swapchain,
	};
	r = vkCreateSwapchainKHR(vk->device, &swch, allocator, &vk->swapchain);
	if (r == VK_SUCCESS) {
#ifndef NDEBUG
		uint32_t old_count = vk->count;
#endif
		// Хорошо бы исключить лишний вызов при old_swapchain, но валидатор:
		// UNASSIGNED-CoreValidation-SwapchainInvalidCount(ERROR / SPEC): msgNum: 442632974 - Validation Error: [ UNASSIGNED-CoreValidation-SwapchainInvalidCount ] Object 0: handle = 0x56272ef7a440, type = VK_OBJECT_TYPE_DEVICE; | MessageID = 0x1a620b0e | vkGetSwapchainImagesKHR() called with non-NULL pSwapchainImages, and with pSwapchainImageCount set to a value (4) that is greater than the value (0) that was returned when pSwapchainImages was NULL.
		vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->count, NULL);
		assert(old_count == vk->count || !vk->old_swapchain);
#ifdef FH_VK_DETAILED_LOG
		printf(" Подготавливается формирователь видеоряда %ux%ux%u:\n",
		        swch.imageExtent.width, swch.imageExtent.height, vk->count);
#endif
		VkImage *images = calloc(vk->count, sizeof(*images));
		vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->count, images);
		if (!vk->old_swapchain) {
			vk->frame = calloc(1 + vk->count, sizeof(*vk->frame));
		}
		if (!images || !vk->frame)
			r = VK_ERROR_OUT_OF_HOST_MEMORY;
		else {
			for (int i = 0; i < vk->count; ++i) {
				vk->frame[i].img = images[i];
				if (!vk->old_swapchain) {
					r = vkCreateSemaphore(vk->device, &ssci, allocator, &vk->frame[i].pool);
					if (r != VK_SUCCESS)
						break;
				}
			}
			if (!vk->old_swapchain)
				printf("  Созданы семафоры захвата кадров (%u).\n", vk->count);
		}
		free(images);
	}
	// Описатели активного кадра старого видеоряда копируем в резервную позицию
	// для отложенного удаления после освобождения буфера команд
	// \see vk_begin_render_cmd(), остальные освобождаем сразу.
	if (vk->old_swapchain) {
#if FH_VK_SWAPCHAIN_LAZY_FREE
		// Проверим занятость буфера команд по VK_TIMEOUT
		// TODO без этой команды валидатор рапортует о занятости ДРУГОГО буфера.
		// TODO изредка при 0-м ожидании валидатор выдаёт при отложенном освобождении:
		// VUID-vkFreeCommandBuffers-pCommandBuffers-00047(ERROR / SPEC): msgNum: 448332540 - Validation Error: [ VUID-vkFreeCommandBuffers-pCommandBuffers-00047 ] Object 0: handle = 0x56527e5e2010, type = VK_OBJECT_TYPE_COMMAND_BUFFER; | MessageID = 0x1ab902fc | Attempt to free VkCommandBuffer 0x56527e5e2010[] which is in use. The Vulkan spec states: All elements of pCommandBuffers must not be in the pending state (https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkFreeCommandBuffers-pCommandBuffers-00047)
		// VUID-vkDestroyFramebuffer-framebuffer-00892(ERROR / SPEC): msgNum: -617577710 - Validation Error: [ VUID-vkDestroyFramebuffer-framebuffer-00892 ] Object 0: handle = 0x56527e5a0580, type = VK_OBJECT_TYPE_DEVICE; | MessageID = 0xdb308312 | Cannot call vkDestroyFramebuffer on VkFramebuffer 0x2540000000254[] that is currently in use by a command buffer. The Vulkan spec states: All submitted commands that refer to framebuffer must have completed execution (https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkDestroyFramebuffer-framebuffer-00892)
		// VUID-vkDestroyImageView-imageView-01026(ERROR / SPEC): msgNum: 1672225264 - Validation Error: [ VUID-vkDestroyImageView-imageView-01026 ] Object 0: handle = 0x56527e5a0580, type = VK_OBJECT_TYPE_DEVICE; | MessageID = 0x63ac21f0 | Cannot call vkDestroyImageView on VkImageView 0x2530000000253[] that is currently in use by a command buffer. The Vulkan spec states: All submitted commands that refer to imageView must have completed execution (https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkDestroyImageView-imageView-01026)
		// при этом vkDeviceWaitIdle(vk->device) перед освобождением
		// в vk_begin_render_cmd() поведение не меняет.
		VkResult busy = vkWaitForFences(vk->device, 1, &vk->frame[vk->active].pending, VK_TRUE, 0);
#else
		VkResult busy = 0;
		// Ждём завершения активной стадии.
		vkWaitForFences(vk->device, 1, &vk->frame[vk->active].pending, VK_TRUE, UINT64_MAX);
#endif
		if (busy) {
			vk->frame[vk->count].cmd     = vk->frame[vk->active].cmd;
			vk->frame[vk->count].fb      = vk->frame[vk->active].fb;
			vk->frame[vk->count].view    = vk->frame[vk->active].view;
			vk->frame[vk->count].pending = vk->frame[vk->active].pending;
		}
		for (int i = vk->count - 1; i >= 0; --i) {
			if (!busy || i != vk->active) {
				if (vk->frame[i].cmd)
					vkFreeCommandBuffers(vk->device, vk->command_pool, 1, &vk->frame[i].cmd);
				if (vk->frame[i].fb)
					vkDestroyFramebuffer(vk->device, vk->frame[i].fb, allocator);
				if (vk->frame[i].view)
					vkDestroyImageView(vk->device, vk->frame[i].view, allocator);
			}
			vk->frame[i].cmd  = VK_NULL_HANDLE;
			vk->frame[i].fb   = VK_NULL_HANDLE;
			vk->frame[i].view = VK_NULL_HANDLE;
		}
		if (!busy) {
			vkDestroySwapchainKHR(vk->device, vk->old_swapchain, allocator);
			vk->old_swapchain = VK_NULL_HANDLE;
		}
	}
	return r;
}

static VkResult create_render(struct vk_context *vk)
{
	const struct VkAttachmentDescription color_attachment = {
		.format        	= vk->format.format,
		.samples       	= VK_SAMPLE_COUNT_1_BIT,
		.loadOp        	= VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp       	= VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp 	= VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp	= VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout 	= VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout   	= VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	static const struct VkAttachmentReference color_attachment_ref = {
		.attachment	= 0,
		.layout    	= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	static const struct VkSubpassDescription subpass = {
		.pipelineBindPoint      	= VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount   	= 0,
		.pInputAttachments      	= NULL,
		.colorAttachmentCount   	= 1,
		.pColorAttachments      	= &color_attachment_ref,
		.pResolveAttachments    	= NULL,
		.pDepthStencilAttachment	= NULL,
		.preserveAttachmentCount	= 0,
		.pPreserveAttachments   	= NULL,
	};
	static const struct VkSubpassDependency dependency = {
		.srcSubpass     	= VK_SUBPASS_EXTERNAL,
		.dstSubpass     	= 0,
		.srcStageMask   	= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask   	= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask  	= 0,
		.dstAccessMask  	= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags	= 0,
	};
	const struct VkRenderPassCreateInfo render_pass_info = {
		.sType          	= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount	= 1,
		.pAttachments   	= &color_attachment,
		.subpassCount   	= 1,
		.pSubpasses     	= &subpass,
		.dependencyCount	= 1,
		.pDependencies  	= &dependency,
	};
	VkResult r = vkCreateRenderPass(vk->device, &render_pass_info, allocator, &vk->render_pass);
	if (r == VK_SUCCESS)
		printf("  Создано описание визуализатора.\n");
	return r;
}

static VkResult create_command_pool(struct vk_context *vk)
{
	const struct VkCommandPoolCreateInfo cmdpoolinfo = {
		.sType           	= VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags           	= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex	= vk->qi[vk_graphics],
	};
	VkResult r = vkCreateCommandPool(vk->device, &cmdpoolinfo, allocator, &vk->command_pool);
	if (r == VK_SUCCESS)
		printf("  Создано хранилище команд графического процессора.\n");
	return r;
}

_Alignas(uint32_t)
static const uint8_t shader_vert_spv[] = {
#include "shader.vert.spv.inl"
};

_Alignas(uint32_t)
static const uint8_t shader_frag_spv[] = {
#include "shader.frag.spv.inl"
};

static VkResult create_shaders(struct vk_context *vk)
{
	static const char *shader_name[] = { "вершин", "фрагментов" };
	static const struct VkShaderModuleCreateInfo shader_mods[] = {
		{
			.sType   	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize	= sizeof(shader_vert_spv),
			.pCode   	= (const uint32_t*)shader_vert_spv,
		},{
			.sType   	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize	= sizeof(shader_frag_spv),
			.pCode   	= (const uint32_t*)shader_frag_spv,
		},
	};
	static_assert(sizeof(shader_mods)/sizeof(*shader_mods) == sizeof(vk->shader)/sizeof(*vk->shader), "Несоответствие модулей шейдеров.");
	VkResult r;
	for (int i = 0; i < sizeof(shader_mods)/sizeof(*shader_mods); ++i) {
		r = vkCreateShaderModule(vk->device, &shader_mods[i], allocator, &vk->shader[i]);
		if (r == VK_SUCCESS)
			printf("  Создан модуль ретушёра %s (%lu байт).\n", shader_name[i], shader_mods[i].codeSize);
	}
	return r;
}

/** Создаёт конвейер. При повторных вызовах использует предыдущий в качестве базы. */
static VkResult create_pipeline(struct vk_context *vk)
{
	const struct VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage 	= VK_SHADER_STAGE_VERTEX_BIT,
			.module	= vk->shader[0],
			.pName 	= "main",
		},{
			.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage 	= VK_SHADER_STAGE_FRAGMENT_BIT,
			.module	= vk->shader[1],
			.pName 	= "main",
		},
	};
	static_assert(sizeof(shader_stages)/sizeof(*shader_stages) == sizeof(vk->shader)/sizeof(*vk->shader), "Несоответствие стадий шейдеров.");
	static const struct VkVertexInputBindingDescription vertex_binding = {
		.binding  	= 0,
		.stride   	= sizeof(struct vertex),
		.inputRate	= VK_VERTEX_INPUT_RATE_VERTEX,
	};
	static const struct VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location	= 0,
			.binding 	= 0,
			.format  	= VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset  	= offsetof(struct vertex, pos),
		}, {
			.location	= 1,
			.binding 	= 0,
			.format  	= VK_FORMAT_R32G32B32A32_SFLOAT,
			.offset  	= offsetof(struct vertex, color),
		},
	};
	static const struct VkPipelineVertexInputStateCreateInfo vertexinput_state = {
		.sType                          	= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount  	= 1,
		.pVertexBindingDescriptions     	= &vertex_binding,
		.vertexAttributeDescriptionCount	= 2,
		.pVertexAttributeDescriptions   	= vertex_attributes,
	};
	static const struct VkPipelineInputAssemblyStateCreateInfo inputassembly_state = {
		.sType                 	= VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology              	= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable	= VK_FALSE,
	};

	const struct VkViewport viewport = {
		.x       	= 0.0,
		.y       	= 0.0,
		.width   	= vk->extent.width,
		.height  	= vk->extent.height,
		.minDepth	= 0.0,
		.maxDepth	= 1.0,
	};
	const struct VkRect2D scissor = {
		.offset	= { 0, 0 },
		.extent	= vk->extent,
	};
	const struct VkPipelineViewportStateCreateInfo viewport_state = {
		.sType        	= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount	= 1,
		.pViewports   	= &viewport,
		.scissorCount 	= 1,
		.pScissors    	= &scissor,
	};

	static const struct VkPipelineRasterizationStateCreateInfo rasterization_state = {
		.sType                  	= VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable       	= VK_FALSE,
		.rasterizerDiscardEnable	= VK_FALSE,
		.polygonMode            	= VK_POLYGON_MODE_FILL,
		.cullMode               	= VK_CULL_MODE_BACK_BIT,
		.frontFace              	= VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable        	= VK_FALSE,
		.depthBiasConstantFactor	= 0,
		.depthBiasClamp         	= 0,
		.depthBiasSlopeFactor   	= 0,
		.lineWidth              	= 1.0,
	};
	static const struct VkPipelineMultisampleStateCreateInfo multisample_state = {
		.sType                	= VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples 	= VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable  	= VK_FALSE,
		.minSampleShading     	= 0.0,
		.pSampleMask          	= NULL,
		.alphaToCoverageEnable	= VK_FALSE,
		.alphaToOneEnable     	= VK_FALSE,
	};
	static const struct VkPipelineColorBlendAttachmentState cb_attach = {
		.blendEnable        	= VK_TRUE,
		.srcColorBlendFactor	= VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp       	= VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor	= VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor	= VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp       	= VK_BLEND_OP_ADD,
		.colorWriteMask     	= VK_COLOR_COMPONENT_R_BIT
		                    	| VK_COLOR_COMPONENT_G_BIT
		                    	| VK_COLOR_COMPONENT_B_BIT
		                    	| VK_COLOR_COMPONENT_A_BIT,
	};
	static const struct VkPipelineColorBlendStateCreateInfo colorblend_state = {
		.sType          	= VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable  	= VK_FALSE,
		.logicOp        	= VK_LOGIC_OP_COPY,
		.attachmentCount	= 1,
		.pAttachments   	= &cb_attach,
		.blendConstants 	= { 0.0, 0.0, 0.0, 0.0, },
	};
	static const struct VkPushConstantRange push_constant = {
		.stageFlags	= VK_SHADER_STAGE_VERTEX_BIT,
		.offset    	= 0,
		.size      	= sizeof(struct transform),
	};
	static const struct VkPipelineLayoutCreateInfo pipelinelayoutinfo = {
		.sType                 	= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount        	= 0,
		.pSetLayouts           	= NULL,
		.pushConstantRangeCount	= 1,
		.pPushConstantRanges   	= &push_constant,
	};
	VkResult r = VK_SUCCESS;
	if (!vk->pipeline_layout) {
		r = vkCreatePipelineLayout(vk->device, &pipelinelayoutinfo, allocator, &vk->pipeline_layout);
#ifdef FH_VK_DETAILED_LOG
		if (r == VK_SUCCESS)
			printf("  Создана топология конвейера.\n");
#endif
	}
	if (r == VK_SUCCESS) {
		// При новом вызове используем предыдущий конвейер в качестве базового.
		// Старый базовый откладываем для освобождения в vk_begin_render_cmd().
		vk->old_pipeline  = vk->base_pipeline;
		vk->base_pipeline = vk->graphics_pipeline;
		const bool first = !vk->graphics_pipeline;
		const struct VkGraphicsPipelineCreateInfo pipelineinfo = {
			.sType              	= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.flags              	= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT
			                    	| (first ? 0 : VK_PIPELINE_CREATE_DERIVATIVE_BIT),
			.stageCount         	= sizeof(shader_stages)/sizeof(*shader_stages),
			.pStages            	= shader_stages,
			.pVertexInputState  	= &vertexinput_state,
			.pInputAssemblyState	= &inputassembly_state,
			.pTessellationState 	= NULL,
			.pViewportState     	= &viewport_state,
			.pRasterizationState	= &rasterization_state,
			.pMultisampleState  	= &multisample_state,
			.pDepthStencilState 	= NULL,
			.pColorBlendState   	= &colorblend_state,
			.pDynamicState      	= NULL,
			.layout             	= vk->pipeline_layout,
			.renderPass         	= vk->render_pass,
			.subpass            	= 0,
			.basePipelineHandle 	= vk->base_pipeline,
			.basePipelineIndex  	= -1,
		};
		r = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipelineinfo,
		                              allocator, &vk->graphics_pipeline);
#ifdef FH_VK_DETAILED_LOG
		if (r == VK_SUCCESS)
			printf("  Создан %s.\n", first ? "базовый конвейер" : "конвейер растеризации");
#endif
	}
	return r;
}

static void destroy_shaders(struct vk_context *vk)
{
	for (int i = sizeof(vk->shader)/sizeof(*vk->shader)-1; i >= 0; --i)
		vkDestroyShaderModule(vk->device, vk->shader[i], allocator);
}


static
VkResult create_buffer(struct vk_context *vk, VkDeviceSize size,
                       VkBufferUsageFlags usage, VkMemoryPropertyFlags flags,
                       VkBuffer *buffer, VkDeviceMemory *mem)
{
	const struct VkBufferCreateInfo buf_info = {
		.sType                	= VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size                 	= size,
		.usage                	= usage,
		.sharingMode          	= VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount	= 0,
		.pQueueFamilyIndices  	= NULL,
	};
	VkResult r = vkCreateBuffer(vk->device, &buf_info, allocator, buffer);
	if (r == VK_SUCCESS) {
#ifdef FH_VK_DETAILED_LOG
		printf("  Создаётся буфер (%#x) %lu байт:", usage, size);
#endif
		struct VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(vk->device, *buffer, &req);
		struct VkPhysicalDeviceMemoryProperties props;
		vkGetPhysicalDeviceMemoryProperties(vk->gpu, &props);
		for ( uint32_t i = 0; i < props.memoryTypeCount; ++i)
			// Для memoryTypeBits гарантируется минимум 1 установленный бит.
			// Для memoryTypes гарантируется, что для младших элементов массива:
			// - propertyFlags являются строгими подмножествами старших;
			// - propertyFlags совпадает с одним из старших, но производительность выше;
			// Таким образом, при сравнении с замаскированным значением,
			// первый элемент окажется требуемым, либо наилучшим.
			if (req.memoryTypeBits & (1 << i) && flags == (props.memoryTypes[i].propertyFlags & flags)) {
				const struct VkMemoryAllocateInfo alloc_info = {
					.sType          	= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
					.allocationSize 	= req.size,
					.memoryTypeIndex	= i,
				};
				r = vkAllocateMemory(vk->device, &alloc_info, allocator, mem);
				if (r == VK_SUCCESS) {
#ifdef FH_VK_DETAILED_LOG
					printf(" память распределена");
#endif
					r = vkBindBufferMemory(vk->device, *buffer, *mem, 0);
					if (r == VK_SUCCESS) {
#ifdef FH_VK_DETAILED_LOG
						printf(" и привязана.");
#endif
						break;
					} else {
						// Не ясно, имеют ли смысл дальнейшие попытки,
						// но они лучше, чем ничего.
						vkFreeMemory(vk->device, *mem, allocator);
					}
				}
			}
#ifdef FH_VK_DETAILED_LOG
		printf("\n");
#endif
	}
	return r;
}

/** Подготавливает буфер для заполнения. */
static inline
VkResult begin_buffer(struct vk_context *vk, struct vk_buffer *buf,
                      VkDeviceSize size, VkBufferUsageFlags usage, void **dest)
{
	VkResult r = VK_SUCCESS;
	if (buf->buf && buf->size < size) {
		// TODO Ложное срабатывание валидатора, как и в прочих случаях?
		vkWaitForFences(vk->device, 1, &vk->frame[vk->active].pending, VK_TRUE, 0);
		vkFreeMemory(vk->device, buf->mem, allocator);
		vkDestroyBuffer(vk->device, buf->buf, allocator);
		buf->buf = VK_NULL_HANDLE;
		buf->size = 0;
	}
	if (buf->buf == VK_NULL_HANDLE)
		r = create_buffer(vk, size, usage,
		                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		                  &buf->buf, &buf->mem);
	*dest = NULL;
	if (r == VK_SUCCESS) {
		r = vkMapMemory(vk->device, buf->mem, 0, size, 0, dest);
		if (buf->size < size)
			buf->size = size;
	}
	return r;
}

VkResult vk_begin_vertex_buffer(struct vk_context *vk, VkDeviceSize size, struct vertex **dest)
{
	struct vk_frame *f = &vk->frame[vk->active];
	return begin_buffer(vk, &f->vert, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, (void**)dest);
}

void vk_end_vertex_buffer(struct vk_context *vk)
{
	// TODO vkFlushMappedMemoryRanges()
	vkUnmapMemory(vk->device, vk->frame[vk->active].vert.mem);
}

VkResult vk_begin_index_buffer(struct vk_context *vk, VkDeviceSize size, vert_index **dest)
{
	struct vk_frame *f = &vk->frame[vk->active];
	return begin_buffer(vk, &f->indx, size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, (void**)dest);
}

void vk_end_index_buffer(struct vk_context *vk)
{
	// TODO vkFlushMappedMemoryRanges()
	vkUnmapMemory(vk->device, vk->frame[vk->active].indx.mem);
}

VkResult vk_acquire_frame(struct vk_context *vk, int64_t timeout)
{
	// Таймаут UINT64_MAX (бесконечное ожидание) допустим когда количество уже
	// захваченных кадров не превышает разность между размером ряда и
	// minImageCount, возвращённой vkGetPhysicalDeviceSurfaceCapabilities2KHR().
	VkResult r = vkAcquireNextImageKHR(vk->device, vk->swapchain, timeout,
	                                   vk->frame[vk->pool_current].pool,
	                                   VK_NULL_HANDLE, &vk->active);
	// TODO VK_SUBOPTIMAL_KHR
	while(r >= VK_SUCCESS) {
		vk->frame[vk->active].acquired = vk->frame[vk->pool_current].pool;
		vk->pool_current = (vk->pool_current + 1) % vk->count;
		if (vk->frame[vk->active].rendered == VK_NULL_HANDLE) {
			r = vkCreateSemaphore(vk->device, &ssci, allocator, &vk->frame[vk->active].rendered);
			if (r != VK_SUCCESS)
				break;
#ifdef FH_VK_DETAILED_LOG
			printf("   Создан семафор готовности изображения №%u.\n", vk->active);
#endif
		}
		if (vk->frame[vk->active].view == VK_NULL_HANDLE) {
			const struct VkImageViewCreateInfo viewinfo = {
				.sType          	= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image          	= vk->frame[vk->active].img,
				.viewType       	= VK_IMAGE_VIEW_TYPE_2D,
				.format         	= vk->format.format,
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
			r = vkCreateImageView(vk->device, &viewinfo, allocator, &vk->frame[vk->active].view);
			if (r != VK_SUCCESS)
				break;
#ifdef FH_VK_DETAILED_LOG
			printf("   Создана проекция кадра №%u.\n", vk->active);
#endif
		}
		if (vk->frame[vk->active].fb == VK_NULL_HANDLE) {
			const struct VkFramebufferCreateInfo fbinfo = {
				.sType          	= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass     	= vk->render_pass,
				.attachmentCount	= 1,
				.pAttachments   	= &vk->frame[vk->active].view,
				.width          	= vk->extent.width,
				.height         	= vk->extent.height,
				.layers         	= 1,
			};
			r = vkCreateFramebuffer(vk->device, &fbinfo, allocator, &vk->frame[vk->active].fb);
			if (r != VK_SUCCESS)
				break;
#ifdef FH_VK_DETAILED_LOG
			printf("   Создан буфер кадра №%u.\n", vk->active);
#endif
		}
		if (vk->frame[vk->active].cmd == VK_NULL_HANDLE) {
			const struct VkCommandBufferAllocateInfo allocinfo = {
				.sType             	= VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool       	= vk->command_pool,
				.level             	= VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount	= 1,
			};
			r = vkAllocateCommandBuffers(vk->device, &allocinfo, &vk->frame[vk->active].cmd);
			if (r != VK_SUCCESS)
				break;
#ifdef FH_VK_DETAILED_LOG
			printf("   Создан буфер команд №%u.\n", vk->active);
#endif
		}
		if (vk->frame[vk->active].pending == VK_NULL_HANDLE) {
			static const struct VkFenceCreateInfo signaled_fence = {
				.sType	= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.flags	= VK_FENCE_CREATE_SIGNALED_BIT,
			};
			r = vkCreateFence(vk->device, &signaled_fence, allocator, &vk->frame[vk->active].pending);
			if (r != VK_SUCCESS)
				break;
#ifdef FH_VK_DETAILED_LOG
			printf("   Создан барьер буфера команд №%u.\n", vk->active);
#endif
		}
		break;
	}
	return r;
}

VkResult vk_begin_render_cmd(struct vk_context *vk)
{
	if (vk->old_swapchain) {
		assert(vk->frame[vk->count].pending);
		assert(vk->frame[vk->count].cmd);
		assert(vk->frame[vk->count].fb);
		assert(vk->frame[vk->count].view);
		vkWaitForFences(vk->device, 1, &vk->frame[vk->count].pending, VK_TRUE, UINT64_MAX);
		vkFreeCommandBuffers(vk->device, vk->command_pool, 1, &vk->frame[vk->count].cmd);
		vkDestroyFramebuffer(vk->device, vk->frame[vk->count].fb, allocator);
		vkDestroyImageView(vk->device, vk->frame[vk->count].view, allocator);
		vkDestroySwapchainKHR(vk->device, vk->old_swapchain, allocator);
		vk->frame[vk->count].pending = VK_NULL_HANDLE;
		vk->frame[vk->count].cmd  = VK_NULL_HANDLE;
		vk->frame[vk->count].fb   = VK_NULL_HANDLE;
		vk->frame[vk->count].view = VK_NULL_HANDLE;
		vk->old_swapchain = VK_NULL_HANDLE;
	}
	if (vk->old_pipeline) {
		vkDestroyPipeline(vk->device, vk->old_pipeline, allocator);
		vk->old_pipeline = VK_NULL_HANDLE;
	}
	// TODO
	// Без синхронизации по vkQueueWaitIdle() или параметру fence vkQueueSubmit()
	// валидатор рапортует, что буфер команд занят. https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkQueueSubmit-pCommandBuffers-00071
	// Всего в конфигурации 4 кадра в очереди, vkAcquireNextImageKHR()
	// захватывает попеременно первые 2, синхронно с кадровой развёрткой.
	// Т.о. буфер якобы занят на протяжении 2-х кадров.
	// При этом замечаний по семафорам, намеренно общим для всех кадров, нет; и
	// semaphore[1] сигналится по завершению исполнения команд буфера,
	// то есть _одновременно_ с fence.
	// Достаточно вызвать vkWaitForFences с тайм-аутом 0.
	vkWaitForFences(vk->device, 1, &vk->frame[vk->active].pending, VK_TRUE, 0);
	vkResetFences(vk->device, 1, &vk->frame[vk->active].pending);

	static const struct VkCommandBufferBeginInfo buf_begin = {
		.sType           	= VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pInheritanceInfo	= NULL,
	};
	VkResult r = vkBeginCommandBuffer(vk->frame[vk->active].cmd, &buf_begin);
	if (r == VK_SUCCESS) {
		static const union VkClearValue cc = {
			.color.float32	= { 0.0, 0.0, 0.0, 0.0 },
		};
		const struct VkRenderPassBeginInfo rpinfo = {
			.sType          	= VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass     	= vk->render_pass,
			.framebuffer    	= vk->frame[vk->active].fb,
			.renderArea     	= {
				.offset	= { 0, 0 },
				.extent	= vk->extent,
			},
			.clearValueCount	= 1,
			.pClearValues   	= &cc,
		};
		vkCmdBeginRenderPass(vk->frame[vk->active].cmd, &rpinfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(vk->frame[vk->active].cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk->graphics_pipeline);
	}
	return r;
}

void vk_cmd_push_transform(struct vk_context *vk, const struct transform *tf)
{
	vkCmdPushConstants(vk->frame[vk->active].cmd, vk->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
	                   0, sizeof(struct transform), tf);
}

void vk_cmd_draw_vertices(struct vk_context *vk, uint32_t count, uint32_t first)
{
	vkCmdBindVertexBuffers(vk->frame[vk->active].cmd, 0, 1, &vk->frame[vk->active].vert.buf, &(VkDeviceSize){0});
	vkCmdDraw(vk->frame[vk->active].cmd, count, 1, first, 0);
}

void vk_cmd_draw_indexed(struct vk_context *vk, uint32_t count)
{
	vkCmdBindVertexBuffers(vk->frame[vk->active].cmd, 0, 1, &vk->frame[vk->active].vert.buf, &(VkDeviceSize){0});
	VkIndexType index_type;
	switch (sizeof(vert_index)) {
		default: assert(0);
		case sizeof(uint16_t): index_type = VK_INDEX_TYPE_UINT16; break;
		case sizeof(uint32_t): index_type = VK_INDEX_TYPE_UINT32; break;
	}
	vkCmdBindIndexBuffer(vk->frame[vk->active].cmd, vk->frame[vk->active].indx.buf, 0, index_type);
	vkCmdDrawIndexed(vk->frame[vk->active].cmd, count, 1, 0, 0, 0);
}

VkResult vk_end_render_cmd(struct vk_context *vk)
{
	vkCmdEndRenderPass(vk->frame[vk->active].cmd);
	// Спецификация Вулкан требует:
	// Если буфер команд является основным, не должно быть
	// активных инстанций RenderPass.
	// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkEndCommandBuffer-commandBuffer-00060
	VkResult r = vkEndCommandBuffer(vk->frame[vk->active].cmd);
	if (r == VK_SUCCESS) {
		static const VkPipelineStageFlags wait_stages[] = {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		};
		const struct VkSubmitInfo gfxcmd = {
			.sType               	= VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount  	= 1,
			.pWaitSemaphores     	= &vk->frame[vk->active].acquired,
			.pWaitDstStageMask   	= wait_stages,
			.commandBufferCount  	= 1,
			.pCommandBuffers     	= &vk->frame[vk->active].cmd,
			.signalSemaphoreCount	= 1,
			.pSignalSemaphores   	= &vk->frame[vk->active].rendered,
		};
		r = vkQueueSubmit(vk->queue[vk_graphics], 1, &gfxcmd, vk->frame[vk->active].pending);
	}
	return r;
}

VkResult vk_present_frame(struct vk_context *vk)
{
	const struct VkPresentInfoKHR present = {
		.sType             	= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount	= 1,
		.pWaitSemaphores   	= &vk->frame[vk->active].rendered,
		.swapchainCount    	= 1,
		.pSwapchains       	= &vk->swapchain,
		.pImageIndices     	= &vk->active,
		.pResults          	= NULL,
	};
	VkResult r = vkQueuePresentKHR(vk->queue[vk_presentation], &present);
	assert(r >= VK_SUCCESS);
	return r;
}

/** Удаляет оконную поверхность и связанные структуры. */
void vk_window_destroy(void *vk_context)
{
	struct vk_context *vk = vk_context;
	vkDeviceWaitIdle(vk->device);
	// Обрабатываем так же и резервный элемент массива,
	// где могут остаться отложенные для удаления описатели.
	do {
		vkDestroySemaphore(vk->device, vk->frame[vk->count].pool, allocator);
		// Из-за ленивой инициализации часть может быть пуста.
		vkDestroySemaphore(vk->device, vk->frame[vk->count].rendered, allocator);
		vkDestroyFence(vk->device,  vk->frame[vk->count].pending, allocator);
		vkFreeCommandBuffers(vk->device, vk->command_pool, 1, &vk->frame[vk->count].cmd);
		vkDestroyFramebuffer(vk->device, vk->frame[vk->count].fb, allocator);
		vkDestroyImageView(vk->device, vk->frame[vk->count].view, allocator);
		vkFreeMemory(vk->device, vk->frame[vk->count].vert.mem, allocator);
		vkDestroyBuffer(vk->device, vk->frame[vk->count].vert.buf, allocator);
		vkFreeMemory(vk->device, vk->frame[vk->count].indx.mem, allocator);
		vkDestroyBuffer(vk->device, vk->frame[vk->count].indx.buf, allocator);
	} while (vk->count--);
	free(vk->frame);

	destroy_shaders(vk);
	vkDestroyCommandPool(vk->device, vk->command_pool, allocator);
	vkDestroyPipeline(vk->device, vk->graphics_pipeline, allocator);
	vkDestroyPipeline(vk->device, vk->base_pipeline, allocator);
	vkDestroyPipeline(vk->device, vk->old_pipeline, allocator);
	vkDestroyPipelineLayout(vk->device, vk->pipeline_layout, allocator);
	vkDestroyRenderPass(vk->device, vk->render_pass, allocator);
	vkDestroySwapchainKHR(vk->device, vk->swapchain, allocator);
	vkDestroySwapchainKHR(vk->device, vk->old_swapchain, allocator);

	vkDestroyDevice(vk->device, allocator);
	vkDestroySurfaceKHR(instance, vk->surface, allocator);
	free(vk);
}

void vk_window_resize(void *p, uint32_t width, uint32_t height)
{
	struct vk_context *vk = p;
	if (!vk->old_swapchain && !vk->old_pipeline) {
		create_swapchain(vk, width, height);
		// TODO Пересоздание конвеера для смены разрешения не выглядит эффективным,
		// однако, вариант с вызовом vkCmdSetViewport() и vkCmdSetScissor() в
		// vk_begin_render_cmd() приводит к следующим сообщениям валидатора:
		// VUID-vkBeginCommandBuffer-commandBuffer-00049(ERROR / SPEC): msgNum: -2080204129 - Validation Error: [ VUID-vkBeginCommandBuffer-commandBuffer-00049 ] Object 0: handle = 0x55add92449c0, type = VK_OBJECT_TYPE_COMMAND_BUFFER; | MessageID = 0x84029a9f | Calling vkBeginCommandBuffer() on active VkCommandBuffer 0x55add92449c0[] before it has completed. You must check command buffer fence before this call. The Vulkan spec states: commandBuffer must not be in the recording or pending state (https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkBeginCommandBuffer-commandBuffer-00049)
		// VUID-vkQueueSubmit-pCommandBuffers-00071(ERROR / SPEC): msgNum: 774851941 - Validation Error: [ VUID-vkQueueSubmit-pCommandBuffers-00071 ] Object 0: handle = 0x55add9225540, type = VK_OBJECT_TYPE_DEVICE; | MessageID = 0x2e2f4d65 | VkCommandBuffer 0x55add92449c0[] is already in use and is not marked for simultaneous use. The Vulkan spec states: If any element of the pCommandBuffers member of any element of pSubmits was not recorded with the VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, it must not be in the pending state (https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VUID-vkQueueSubmit-pCommandBuffers-00071)
		create_pipeline(vk);
	}
}

static VkResult context_init(struct vk_context *vk, uint32_t width, uint32_t height)
{
	VkResult r;
	if ((r = select_gpu(vk)) != VK_SUCCESS)
		return r;
	if ((r = create_device(vk)) != VK_SUCCESS)
		return r;

	if ((r = create_swapchain(vk, width, height)) != VK_SUCCESS)
		return r;
	if ((r = create_render(vk)) != VK_SUCCESS)
		return r;
	if ((r = create_shaders(vk)) != VK_SUCCESS)
		return r;
	if ((r = create_pipeline(vk)) != VK_SUCCESS)
		return r;
	return create_command_pool(vk);
}

void vk_window_create(window_server *display, window_surface window,
                      uint32_t width, uint32_t height, void **vk_context)
{
	struct vk_context *vk = calloc(1, sizeof(*vk));
	*vk_context = vk;
	if (!vk)
		return;
	if (surface_create(&vk->surface, display, window) != VK_SUCCESS
	 || context_init(vk, width, height) != VK_SUCCESS) {
		vk_window_destroy(vk);
		*vk_context = NULL;
	}
}
