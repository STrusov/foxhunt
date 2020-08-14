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

#include "ay_music.h"
#include "vulkan.h"
#include "wayland_window.h"

typedef uint16_t vert_index;
typedef unsigned fast_index;

#include "triangulatio.h"

const float PI = 3.14159f;

static bool draw_frame(void *p)
{
	ay_music_continue(5);

	struct vk_context *vk = p;
	VkResult r = vk_acquire_frame(vk);

	const unsigned cnt = 6;
	void *vert_buf;
	r = vk_begin_vertex_buffer(vk, (1 + cnt) * sizeof(struct vertex2d), &vert_buf);
	struct vertex2d *vert = vert_buf;
	static float angle;
	angle = angle + PI/192;
#if 01
	vert->pos.x = 0;
	vert->pos.y = 0;
	vert->color.r = 0.5f;
	vert->color.g = 0.5f;
	vert->color.b = 0.5f;
	++vert;
#endif
	for (unsigned i = 0; i < cnt; ++i) {
		vert->pos.x = 0.95f * cosf(angle + i * 2*PI/cnt);
		vert->pos.y = 0.95f * sinf(angle + i * 2*PI/cnt);
		vert->color.r = 0.3f + 0.7f * cosf(2 * angle + i * 2*PI/(4*cnt));
		vert->color.g = 0.3f + 0.7f * cosf(3 * angle + i * 2*PI/(3*cnt));
		vert->color.b = 0.3f + 0.7f * cosf(4 * angle + i * 2*PI/(2*cnt));
		++vert;
	}
	const unsigned vcnt = vert - (struct vertex2d*)vert_buf;

	unsigned tcnt = vcnt;
	struct tri_index *indx;
	void *indx_buf;
	r = vk_begin_index_buffer(vk, tcnt * sizeof(*indx), &indx_buf);
	indx = indx_buf;
	triangulate(vert_buf, vcnt, &indx, &tcnt);

	vk_end_vertex_buffer(vk);
	vk_end_index_buffer(vk);

	r = vk_begin_render_cmd(vk);
		vk_cmd_draw_indexed(vk, 3 * tcnt);
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

