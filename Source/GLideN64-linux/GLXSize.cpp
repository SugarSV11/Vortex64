// The size of the drawable that is current in the calling thread, from the X server: SDL learns the
// size of a window that it did not create (the render widget of the GUI) only from events that
// the toolkit that owns the window may not leave to it. Separate from the code that includes the
// headers of GLideN64 because those define macros over the names of OpenGL.
#include <GL/glx.h>

bool glxDrawableSize(int & _width, int & _height)
{
	Display * display = glXGetCurrentDisplay();
	const GLXDrawable drawable = glXGetCurrentDrawable();
	if (display == nullptr || drawable == 0)
		return false;
	unsigned int w = 0, h = 0;
	glXQueryDrawable(display, drawable, GLX_WIDTH, &w);
	glXQueryDrawable(display, drawable, GLX_HEIGHT, &h);
	if (w == 0 || h == 0)
		return false;
	_width = static_cast<int>(w);
	_height = static_cast<int>(h);
	return true;
}
