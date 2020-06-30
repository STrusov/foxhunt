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
 */

#include "wayland_window.h"

int main(int argc, char *argv[])
{
	if (!wayland_init())
		return 1;

	struct window window = {
		.title	= "Окно",
		.width	= 640,
		.height	= 480,
	};
	window_create(&window);
	window_dispatch();

	wayland_stop();

	return 0;
}

