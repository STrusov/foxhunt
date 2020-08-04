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
	struct vk_context *vk = p;
	VkResult r = vk_acquire_frame(vk);

	const int cnt = 3;
	void *dest;
	r = vk_begin_vertex_buffer(vk, cnt * sizeof(struct vertex2d), &dest);
	struct vertex2d *vert = dest;
	static float angle;
	angle = angle + PI/192;
	for (int i = 0; i < cnt; ++i) {
		vert[i].pos.x = 0.95f * cosf(angle + i * 2*PI/cnt - PI/2);
		vert[i].pos.y = 0.95f * sinf(angle + i * 2*PI/cnt - PI/2);
		vert[i].color.r = 0.3f + 0.7f * cosf(2 * angle + i * 2*PI/(cnt+3));
		vert[i].color.g = 0.3f + 0.7f * cosf(3 * angle + i * 2*PI/(cnt+2));
		vert[i].color.b = 0.3f + 0.7f * cosf(4 * angle + i * 2*PI/(cnt+1));
	}
	vk_end_vertex_buffer(vk);

	r = vk_begin_render_cmd(vk);
		vk_cmd_draw_vertices(vk, cnt, 0);
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

	printf("Выход.\n");
	return 0;
}

