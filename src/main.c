/**
 * Пример клиента Wayland основан на следующих работах:
 *
 * Drew DeVault "The Wayland Protocol"
 * https://wayland-book.com
 *
 * Jan Newmarch "Programming Wayland Clients"
 * https://jan.newmarch.name/Wayland/
 *
 * Библиотека GLFW https://www.glfw.org
 * https://github.com/glfw/glfw/blob/master/README.md
 *
 *
 * Клиент Vulkan основан на https://vulkan-tutorial.com
 * https://github.com/Overv/VulkanTutorial/graphs/contributors
 *
 */

#include <stdio.h>

#include "ay_music.h"
#include "vulkan.h"
#include "wayland_window.h"

const float PI = 3.14159f;

static bool draw_frame(void *p)
{
	ay_music_continue(5);

	struct vk_context *vk = p;
	VkResult r = vk_acquire_frame(vk);

	const unsigned cnt = 3;
	void *dest;
	r = vk_begin_vertex_buffer(vk, cnt * sizeof(struct vertex2d), &dest);
	struct vertex2d *vert = dest;
	static float angle;
	angle = angle + PI/192;
	for (unsigned i = 0; i < cnt; ++i) {
		vert->pos.x = 0.95f * cosf(angle + i * 2*PI/cnt);
		vert->pos.y = 0.95f * sinf(angle + i * 2*PI/cnt);
		vert->color.r = 0.3f + 0.7f * cosf(2 * angle + i * 2*PI/(4*cnt));
		vert->color.g = 0.3f + 0.7f * cosf(3 * angle + i * 2*PI/(3*cnt));
		vert->color.b = 0.3f + 0.7f * cosf(4 * angle + i * 2*PI/(2*cnt));
		++vert;
	}
	vk_end_vertex_buffer(vk);
	const unsigned vcnt = vert - (struct vertex2d*)dest;

	const unsigned icnt = vcnt;
	uint16_t *idx;
	r = vk_begin_index_buffer(vk, icnt * sizeof(*idx), &dest);
	idx = dest;
	for (unsigned i = 0; i < icnt; ++i){
		*idx++ = i;
	}
	vk_end_index_buffer(vk);

	r = vk_begin_render_cmd(vk);
		vk_cmd_draw_indexed(vk, icnt);
	r = vk_end_render_cmd(vk);

	r = vk_present_frame(vk);
	return (r == VK_SUCCESS);
}

static const struct render vulkan = {
	.create    	= vk_window_create,
	.destroy   	= vk_window_destroy,
	.draw_frame	= draw_frame,
	.resize    	= vk_window_resize,
};

int main(int argc, char *argv[])
{
	ay_music_init();
	ay_music_play();

	if (!wayland_init())
		return 1;

	if (vk_init() != VK_SUCCESS)
		return 2;

	struct window window = {
		.render	= &vulkan,
		.title	= "Окно",
		.width	= 640,
		.height	= 640,
		.border = 10,
		.aspect_ratio = 1,
//		.constant_aspect_ratio = true,
	};
	window_create(&window);
	window_dispatch(&window);

	window_destroy(&window);

	vk_stop();
	wayland_stop();
	ay_music_stop();

	printf("Выход.\n");
	return 0;
}

