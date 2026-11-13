// The size of the drawable that is current in the calling thread, from the X server. SDL learns the
// size of a window that it did not create (the render widget of the GUI) only from events that the
// toolkit that owns the window does not always leave to it, so SDL_GL_GetDrawableSize can be stale
// after the GUI changes the size (full screen). Separate from OGLglitchmain.cpp because that
// includes headers that define macros over the names of OpenGL.
#ifndef _WIN32
#include <GL/glx.h>

bool PJ64_GLXDrawableSize(int & width, int & height)
{
    Display * display = glXGetCurrentDisplay();
    const GLXDrawable drawable = glXGetCurrentDrawable();
    if (display == nullptr || drawable == 0)
    {
        return false;
    }
    unsigned int w = 0, h = 0;
    glXQueryDrawable(display, drawable, GLX_WIDTH, &w);
    glXQueryDrawable(display, drawable, GLX_HEIGHT, &h);
    if (w == 0 || h == 0)
    {
        return false;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}
#endif
