// The context part of the Windows display window of GLideN64 (WindowsWGL), on Linux: SDL2 creates
// the OpenGL context on the window that the emulator hands over (GFX_INFO::hWnd, an X11 window
// that SDL embeds with SDL_CreateWindowFrom) or, without one, on a window of its own.
#pragma once
#include <stdint.h>

class WindowsWGL
{
public:
	static bool start();
	static void stop();
	static void swapBuffers();
	// size of the drawable of the window, in pixels
	static bool drawableSize(int & _width, int & _height);
	static void setNativeWindow(void * _window);
	static void * nativeWindow();
	static void setTitle(const char * _title);
};
