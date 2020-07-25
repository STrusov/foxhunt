
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "vulkan.h"

#ifdef ENABLE_VK_VALIDATION
static const char *validation_layers[] = {
	"VK_LAYER_KHRONOS_validation",
};
#endif

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
#ifdef ENABLE_VK_VALIDATION
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
	/** Размер кадра.                                         */
	VkExtent2D      	extent;
	/** Формат элементов изображения.                         */
	VkFormat        	format;
	/** Количество кадров в последовательности.               */
	uint32_t        	count;
	/** Индекс текущего кадра.                                */
	uint32_t        	active;
	/** Последовательность кадров.                            */
	struct vk_frame 	*frame;

	/** Представляет коллекцию привязок, шагов и зависимостей между ними. */
	VkRenderPass    	render_pass;
	/** Хранилище команд для графического процессора          */
	VkCommandPool   	command_pool;

	/** Графический конвейер                                  */
	VkPipeline      	graphics_pipeline;
	/** и описатель его топологии                             */
	VkPipelineLayout	pipeline_layout;

	/** Флажки синхронизации очередей.                        */
	VkSemaphore     	semaphore[2];
};

/** Буфер кадра, проекция и команды построения изображения. */
struct vk_frame {
	VkImage         	img;
	VkFramebuffer   	fb;
	VkImageView     	view;
	VkCommandBuffer 	cmd;
	VkFence         	pending;	///< готовность буфера команд.
	VkBuffer        	vert_buf;
	VkDeviceMemory  	vert_mem;
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

/** Подготавливает описатель видеоряда. */
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
		printf(" Подготавливается формирователь видеоряда %ux%ux%u:\n",
		        swch.imageExtent.width, swch.imageExtent.height, vk->count);
		VkImage *images = calloc(vk->count, sizeof(*images));
		vkGetSwapchainImagesKHR(vk->device, vk->swapchain, &vk->count, images);
		vk->frame = calloc(vk->count, sizeof(*vk->frame));
		if (!images || !vk->frame)
			r = VK_ERROR_OUT_OF_HOST_MEMORY;
		else
			for (int i = 0; i < vk->count; ++i)
				vk->frame[i].img = images[i];
		free(images);
	}
	free(formats);
	return r;
}

static VkResult create_render(struct vk_context *vk)
{
	const struct VkAttachmentDescription color_attachment = {
		.format        	= vk->format,
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

static VkResult create_pipeline(struct vk_context *vk)
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
	struct VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage 	= VK_SHADER_STAGE_VERTEX_BIT,
			.module	= VK_NULL_HANDLE,
			.pName 	= "main",
		},{
			.sType 	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage 	= VK_SHADER_STAGE_FRAGMENT_BIT,
			.module	= VK_NULL_HANDLE,
			.pName 	= "main",
		},
	};
	static_assert(sizeof(shader_mods)/sizeof(*shader_mods) == sizeof(shader_stages)/sizeof(*shader_stages));
	VkResult r;
	for (int i = 0; i < sizeof(shader_mods)/sizeof(*shader_mods); ++i) {
		r = vkCreateShaderModule(vk->device, &shader_mods[i], allocator, &shader_stages[i].module);
		if (r != VK_SUCCESS)
			goto exit_with_cleanup;
		printf("  Создан модуль ретушёра %s (%lu байт).\n", shader_name[i], shader_mods[i].codeSize);
	}

	static const struct VkVertexInputBindingDescription vertex_binding = {
		.binding  	= 0,
		.stride   	= sizeof(struct vertex2d),
		.inputRate	= VK_VERTEX_INPUT_RATE_VERTEX,
	};
	static const struct VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location	= 0,
			.binding 	= 0,
			.format  	= VK_FORMAT_R32G32_SFLOAT,
			.offset  	= offsetof(struct vertex2d, pos),
		}, {
			.location	= 1,
			.binding 	= 0,
			.format  	= VK_FORMAT_R32G32B32_SFLOAT,
			.offset  	= offsetof(struct vertex2d, color),
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
		.blendEnable        	= VK_FALSE,
		.srcColorBlendFactor	= VK_BLEND_FACTOR_ZERO,
		.dstColorBlendFactor	= VK_BLEND_FACTOR_ZERO,
		.colorBlendOp       	= VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor	= VK_BLEND_FACTOR_ZERO,
		.dstAlphaBlendFactor	= VK_BLEND_FACTOR_ZERO,
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
	static const struct VkPipelineLayoutCreateInfo pipelinelayoutinfo = {
		.sType                 	= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount        	= 0,
		.pSetLayouts           	= NULL,
		.pushConstantRangeCount	= 0,
		.pPushConstantRanges   	= NULL,
	};
	r = vkCreatePipelineLayout(vk->device, &pipelinelayoutinfo, allocator, &vk->pipeline_layout);
	if (r == VK_SUCCESS) {
		printf("  Создана топология конвейера.\n");
		const struct VkGraphicsPipelineCreateInfo pipelineinfo = {
			.sType              	= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
			.basePipelineHandle 	= VK_NULL_HANDLE,
			.basePipelineIndex  	= 0,
		};
		r = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &pipelineinfo,
		                              allocator, &vk->graphics_pipeline);
		if (r == VK_SUCCESS)
			printf("  Создан конвейер.\n");
	}
exit_with_cleanup:
	for (int i = sizeof(shader_mods)/sizeof(*shader_mods)-1; i >= 0; --i)
		vkDestroyShaderModule(vk->device, shader_stages[i].module, allocator);
	return r;
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
		printf("  Создаётся буфер:");
		struct VkMemoryRequirements req;
		vkGetBufferMemoryRequirements(vk->device, *buffer, &req);
		struct VkPhysicalDeviceMemoryProperties props;
		vkGetPhysicalDeviceMemoryProperties(vk->gpu, &props);
		for ( uint32_t i = 0; i < props.memoryTypeCount; ++i)
			if (req.memoryTypeBits & (1 << i) && flags == (props.memoryTypes[i].propertyFlags & flags)) {
				const struct VkMemoryAllocateInfo alloc_info = {
					.sType          	= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
					.allocationSize 	= req.size,
					.memoryTypeIndex	= i,
				};
				r = vkAllocateMemory(vk->device, &alloc_info, allocator, mem);
				if (r == VK_SUCCESS) {
					printf(" память распределена");
					r = vkBindBufferMemory(vk->device, *buffer, *mem, 0);
					if (r == VK_SUCCESS)
						printf(" и привязана.");
				}
			}
		printf("\n");
	}
	return r;
}

/** Подготавливает буфер вершин для заполнения. */
VkResult vk_begin_vertex_buffer(struct vk_context *vk, VkDeviceSize size, void **dest)
{
	VkResult r = VK_SUCCESS;
	if (vk->frame[vk->active].vert_buf == VK_NULL_HANDLE)
		r = create_buffer(vk, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		                  &vk->frame[vk->active].vert_buf, &vk->frame[vk->active].vert_mem);
	*dest = NULL;
	if (r == VK_SUCCESS)
		r = vkMapMemory(vk->device, vk->frame[vk->active].vert_mem, 0, size, 0, dest);
	return r;
}

void vk_end_vertex_buffer(struct vk_context *vk)
{
	// TODO vkFlushMappedMemoryRanges()
	vkUnmapMemory(vk->device, vk->frame[vk->active].vert_mem);
}

VkResult vk_acquire_frame(struct vk_context *vk)
{
	// Таймаут UINT64_MAX (бесконечное ожидание) допустим когда количество уже
	// захваченных кадров не превышает разность между размером ряда и
	// minImageCount, возвращённой vkGetPhysicalDeviceSurfaceCapabilities2KHR().
	VkResult r = vkAcquireNextImageKHR(vk->device, vk->swapchain, 0, vk->semaphore[0],
	                                   VK_NULL_HANDLE, &vk->active);
	while(r == VK_SUCCESS) {
		if (vk->frame[vk->active].view == VK_NULL_HANDLE) {
			const struct VkImageViewCreateInfo viewinfo = {
				.sType          	= VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image          	= vk->frame[vk->active].img,
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
			r = vkCreateImageView(vk->device, &viewinfo, allocator, &vk->frame[vk->active].view);
			if (r != VK_SUCCESS)
				break;
			printf("   Создана проекция кадра №%u.\n", vk->active);
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
			printf("   Создан буфер кадра №%u.\n", vk->active);
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
			printf("   Создан буфер команд №%u.\n", vk->active);
		}
		if (vk->frame[vk->active].pending == VK_NULL_HANDLE) {
			static const struct VkFenceCreateInfo signaled_fence = {
				.sType	= VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
				.flags	= VK_FENCE_CREATE_SIGNALED_BIT,
			};
			r = vkCreateFence(vk->device, &signaled_fence, allocator, &vk->frame[vk->active].pending);
			if (r != VK_SUCCESS)
				break;
			printf("   Создан барьер буфера команд №%u.\n", vk->active);
		}
		break;
	}
	return r;
}

VkResult vk_begin_render_cmd(struct vk_context *vk)
{
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
			.color.float32	= { 0.0, 0.0, 0.0, 1.0 },
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

void vk_cmd_draw_vertices(struct vk_context *vk, uint32_t count, uint32_t first)
{
	vkCmdBindVertexBuffers(vk->frame[vk->active].cmd, 0, 1, &vk->frame[vk->active].vert_buf, &(VkDeviceSize){0});
	vkCmdDraw(vk->frame[vk->active].cmd, count, 1, first, 0);
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
		printf("   Заполнен буфер команд №%u.\n", vk->active);
		static const VkPipelineStageFlags wait_stages[] = {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		};
		const struct VkSubmitInfo gfxcmd = {
			.sType               	= VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount  	= 1,
			.pWaitSemaphores     	= &vk->semaphore[0],
			.pWaitDstStageMask   	= wait_stages,
			.commandBufferCount  	= 1,
			.pCommandBuffers     	= &vk->frame[vk->active].cmd,
			.signalSemaphoreCount	= 1,
			.pSignalSemaphores   	= &vk->semaphore[1],
		};
		r = vkQueueSubmit(vk->queue[vk_graphics], 1, &gfxcmd, vk->frame[vk->active].pending);
	}
	return r;
}

VkResult create_sync_objects(struct vk_context *vk) {
	static const struct VkSemaphoreCreateInfo sc = {
		.sType	= VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	const int n_sem = sizeof(vk->semaphore)/sizeof(*vk->semaphore);
	for (int i = 0; i < n_sem; ++i) {
		VkResult r = vkCreateSemaphore(vk->device, &sc, allocator, &vk->semaphore[i]);
		if (r != VK_SUCCESS)
			return r;
	}
	printf("  Созданы объекты синхронизации (%i).\n", n_sem);
	return VK_SUCCESS;
}

VkResult vk_present_frame(struct vk_context *vk)
{
	const struct VkPresentInfoKHR present = {
		.sType             	= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount	= 1,
		.pWaitSemaphores   	= &vk->semaphore[1],
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
void vk_window_destroy(struct vk_context *vk)
{
	vkDeviceWaitIdle(vk->device);
	for (int i = 0; i < sizeof(vk->semaphore)/sizeof(*vk->semaphore); ++i)
		vkDestroySemaphore(vk->device, vk->semaphore[i], allocator);
	do {
		vkDestroyFence(vk->device,  vk->frame[vk->count].pending, allocator);
		vkFreeCommandBuffers(vk->device, vk->command_pool, 1, &vk->frame[vk->count].cmd);
		vkDestroyFramebuffer(vk->device, vk->frame[vk->count].fb, allocator);
		vkDestroyImageView(vk->device, vk->frame[vk->count].view, allocator);
		vkFreeMemory(vk->device, vk->frame[vk->count].vert_mem, allocator);
		vkDestroyBuffer(vk->device, vk->frame[vk->count].vert_buf, allocator);
	} while (vk->count--);
	free(vk->frame);

	vkDestroyCommandPool(vk->device, vk->command_pool, allocator);
	vkDestroyPipeline(vk->device, vk->graphics_pipeline, allocator);
	vkDestroyPipelineLayout(vk->device, vk->pipeline_layout, allocator);
	vkDestroyRenderPass(vk->device, vk->render_pass, allocator);
	vkDestroySwapchainKHR(vk->device, vk->swapchain, allocator);

	vkDestroyDevice(vk->device, allocator);
	vkDestroySurfaceKHR(instance, vk->surface, allocator);
	free(vk);
}


void vk_window_create(struct wl_display *display, struct wl_surface *surface,
                      uint32_t width, uint32_t height, void **vk_context)
{
	struct vk_context *vk = calloc(1, sizeof(*vk));
	*vk_context = vk;
	if (!vk)
		return;
	while (create_surface(vk, display, surface) == VK_SUCCESS) {
		select_gpu(vk);
		create_device(vk);

		create_swapchain(vk, width, height);
		create_render(vk);
		create_pipeline(vk);
		create_command_pool(vk);
		create_sync_objects(vk);

		return;
	}
	vk_window_destroy(vk);
}

