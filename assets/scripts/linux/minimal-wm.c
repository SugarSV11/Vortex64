/* A window manager of the least that the tests need (Xvfb has none): it maps the windows, grants their
 * configure requests and honours _NET_WM_STATE_FULLSCREEN (the window takes the whole screen, and its old
 * geometry back when the state is removed), the way a real one does for Qt. Not a desktop: no
 * decorations, no focus policy.  Build: cc -o minimal-wm minimal-wm.c -lX11 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXW 64
static struct { Window w; int x, y; unsigned wd, ht; int full; } g_saved[MAXW];

static int Find(Window w)
{
    int i, free_i = -1;
    for (i = 0; i < MAXW; i++)
    {
        if (g_saved[i].w == w) return i;
        if (g_saved[i].w == 0 && free_i < 0) free_i = i;
    }
    if (free_i >= 0) g_saved[free_i].w = w;
    return free_i;
}

static int IgnoreErrors(Display * d, XErrorEvent * e) { (void)d; (void)e; return 0; }

int main(void)
{
    Display * dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "no display\n"); return 1; }
    Window root = DefaultRootWindow(dpy);
    int screen = DefaultScreen(dpy);
    Atom net_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom net_full = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    Atom net_check = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    Atom net_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    XSetErrorHandler(IgnoreErrors);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask);
    Atom supported[] = { net_state, net_full, net_check };
    XChangeProperty(dpy, root, net_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported, 3);
    Window check = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, root, net_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(dpy, check, net_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check, 1);
    XChangeProperty(dpy, check, net_name, utf8, 8, PropModeReplace, (unsigned char *)"minimal-wm", 10);
    XSync(dpy, False);
    printf("minimal-wm: running\n");
    fflush(stdout);
    for (;;)
    {
        XEvent ev;
        XNextEvent(dpy, &ev);
        switch (ev.type)
        {
        case MapRequest:
            XMapWindow(dpy, ev.xmaprequest.window);
            break;
        case ConfigureRequest:
        {
            XConfigureRequestEvent * r = &ev.xconfigurerequest;
            XWindowChanges c;
            int i = Find(r->window);
            if (i >= 0 && g_saved[i].full) break; /* a full screen window keeps the screen */
            c.x = r->x; c.y = r->y; c.width = r->width; c.height = r->height;
            c.border_width = r->border_width; c.sibling = r->above; c.stack_mode = r->detail;
            XConfigureWindow(dpy, r->window, r->value_mask, &c);
            break;
        }
        case ClientMessage:
            if (ev.xclient.message_type == net_state)
            {
                long action = ev.xclient.data.l[0];
                Atom a1 = (Atom)ev.xclient.data.l[1], a2 = (Atom)ev.xclient.data.l[2];
                if (a1 == net_full || a2 == net_full)
                {
                    Window w = ev.xclient.window;
                    int i = Find(w);
                    if (i < 0) break;
                    int want = action == 1 || (action == 2 && !g_saved[i].full);
                    if (want && !g_saved[i].full)
                    {
                        XWindowAttributes at;
                        XGetWindowAttributes(dpy, w, &at);
                        g_saved[i].x = at.x; g_saved[i].y = at.y; g_saved[i].wd = at.width; g_saved[i].ht = at.height;
                        g_saved[i].full = 1;
                        XMoveResizeWindow(dpy, w, 0, 0, DisplayWidth(dpy, screen), DisplayHeight(dpy, screen));
                        XRaiseWindow(dpy, w);
                        XChangeProperty(dpy, w, net_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&net_full, 1);
                    }
                    else if (!want && g_saved[i].full)
                    {
                        g_saved[i].full = 0;
                        XMoveResizeWindow(dpy, w, g_saved[i].x, g_saved[i].y, g_saved[i].wd, g_saved[i].ht);
                        XDeleteProperty(dpy, w, net_state);
                    }
                }
            }
            break;
        case DestroyNotify:
        {
            int i;
            for (i = 0; i < MAXW; i++) if (g_saved[i].w == ev.xdestroywindow.window) memset(&g_saved[i], 0, sizeof g_saved[i]);
            break;
        }
        }
    }
}
