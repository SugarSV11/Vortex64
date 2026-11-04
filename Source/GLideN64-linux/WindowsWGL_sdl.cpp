#include <Graphics/OpenGLContext/windows/WindowsWGL.h>
#include <Config.h>
#include <GLideN64.h>
#include <Graphics/OpenGLContext/GLFunctions.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <string>

bool glxDrawableSize(int & _width, int & _height); // GLXSize.cpp

#include "../Project64-plugin-spec/PresentControl.h"

namespace {
PJ64Present::Control s_present;
unsigned s_contextGeneration = 0; // counts the contexts made (the address of a deleted context can be given again)

bool presentTrace()
{
	static const bool on = getenv("VORTEX_PRESENT_TRACE") != nullptr && getenv("VORTEX_PRESENT_TRACE")[0] == '1';
	return on;
}

SDL_Window * s_window = nullptr;
SDL_GLContext s_context = nullptr;
bool s_ownWindow = false;
bool s_ownVideo = false;
void * s_native = nullptr;
std::atomic<int> s_width{0};
std::atomic<int> s_height{0};
std::string s_title = "GLideN64";

void report(const char * _what)
{
	fprintf(stderr, "[GLideN64] %s: %s\n", _what, SDL_GetError());
}

void updateSize()
{
	int w = 0, h = 0;
	if (s_window != nullptr && !glxDrawableSize(w, h))
		SDL_GL_GetDrawableSize(s_window, &w, &h);
	s_width = w;
	s_height = h;
}
}

void WindowsWGL::setNativeWindow(void * _window)
{
	s_native = _window;
}

void * WindowsWGL::nativeWindow()
{
	return s_native;
}

void WindowsWGL::setTitle(const char * _title)
{
	s_title = _title;
	if (s_ownWindow && s_window != nullptr)
		SDL_SetWindowTitle(s_window, _title);
}

bool WindowsWGL::start()
{
	if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
			report("SDL_InitSubSystem(VIDEO)");
			return false;
		}
		s_ownVideo = true;
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	// the context of the emulator's window: SDL treats a foreign window as an OpenGL one only
	// with this hint (as in Project64-video)
	if (s_native != nullptr) {
		SDL_SetHint(SDL_HINT_VIDEO_FOREIGN_WINDOW_OPENGL, "1");
		s_window = SDL_CreateWindowFrom(s_native);
		if (s_window == nullptr)
			report("SDL_CreateWindowFrom");
	}
	if (s_window == nullptr) {
		s_window = SDL_CreateWindow(s_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			config.video.windowedWidth, config.video.windowedHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
		if (s_window == nullptr) {
			report("SDL_CreateWindow");
			stop();
			return false;
		}
		s_ownWindow = true;
	}

	// the highest core profile that the driver offers, as the WGL code does with
	// wglCreateContextAttribsARB (GLideN64 needs 3.3; more gives it the faster paths)
	static const int versions[][2] = { {4, 6}, {4, 5}, {4, 3}, {4, 1}, {3, 3} };
	for (const auto & v : versions) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v[0]);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v[1]);
		s_context = SDL_GL_CreateContext(s_window);
		if (s_context != nullptr)
			break;
	}
	if (s_context == nullptr) {
		report("SDL_GL_CreateContext");
		stop();
		return false;
	}
	if (SDL_GL_MakeCurrent(s_window, s_context) != 0) {
		report("SDL_GL_MakeCurrent");
		stop();
		return false;
	}

	initGLFunctions();
	updateSize();
	s_contextGeneration++; // (the interval of the new context is the policy's, set at the first swap)

	// the swap interval is set by applySwapInterval() at the first swap (and follows the frontend afterwards)
	return true;
}

void WindowsWGL::stop()
{
	if (s_context != nullptr) {
		SDL_GL_MakeCurrent(s_window, nullptr);
		SDL_GL_DeleteContext(s_context);
		s_context = nullptr;
	}
	if (s_window != nullptr) {
		SDL_DestroyWindow(s_window);
		s_window = nullptr;
	}
	s_ownWindow = false;
	s_width = s_height = 0;
	if (s_ownVideo) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		s_ownVideo = false;
	}
}

// The synchronization of the presentation with the monitor (Project64-plugin-spec/PresentControl.h): the frontend sends the mode (Auto, On,
// Off), the refresh rate of the monitor and the frame rate of the game; the swap interval of the context follows. Without a frontend the
// setting "Vertical sync" of the plugin decides. Looked at again when the context is made again and every 120 swaps, not on every frame.
static void applySwapInterval()
{
	static unsigned context = ~0u;
	static int applied = -99, wanted = -99, printedWanted = -99, printedApplied = -99;
	static bool adaptiveKnown = false, adaptiveOk = false;
	static unsigned swaps = 0;
	if (context != s_contextGeneration) {
		context = s_contextGeneration;
		applied = -99;
		adaptiveKnown = false;
	}
	if (!adaptiveKnown) {
		adaptiveOk = SDL_GL_SetSwapInterval(-1) == 0; // (EXT_swap_control_tear: a late frame is shown at once, the others wait)
		adaptiveKnown = true;
		applied = -99;
	}
	if (++swaps % 120 == 0 && applied != -99) {
		const int now = SDL_GL_GetSwapInterval();
		if (now != applied && !(applied == -1 && now < 0))
			applied = -99;
	}
	const PJ64Present::Plan plan = s_present.Resolve(config.video.verticalSync > 0, adaptiveOk);
	if (plan.Interval == wanted && applied != -99)
		return;
	wanted = plan.Interval;
	int got = 0;
	if (SDL_GL_SetSwapInterval(wanted) == 0) {
		got = wanted;
	} else if (wanted != 0 && wanted != 1 && SDL_GL_SetSwapInterval(1) == 0) {
		got = 1;
	} else {
		SDL_GL_SetSwapInterval(0);
		got = 0;
	}
	applied = got;
	s_present.Record(wanted, applied, adaptiveOk);
	if (presentTrace() && (printedWanted != wanted || printedApplied != applied)) {
		printedWanted = wanted;
		printedApplied = applied;
		fprintf(stderr, "[present] GLideN64: swap interval wanted %d, applied %d (%s)%s\n", wanted, applied, plan.Reason, got != wanted ? " - the driver refused the wanted one" : "");
	}
}

void WindowsWGL::swapBuffers()
{
	if (s_window == nullptr)
		return;
	applySwapInterval();
	// (the maximum FPS of the user: a frame that comes before its turn is not presented; the game does not wait for it)
	if (s_present.Admit())
		SDL_GL_SwapWindow(s_window);
	// the embedded window is resized by the GUI: SDL learns it from the events of the window
	SDL_Event event;
	while (SDL_PollEvent(&event)) {}
	updateSize();
}

// the size of the drawable as of the last swap (the thread of the context swaps, others ask)
bool WindowsWGL::drawableSize(int & _width, int & _height)
{
	_width = s_width;
	_height = s_height;
	return _width > 0 && _height > 0;
}

extern "C" __attribute__((visibility("default"))) void PJ64SetPresentHint(const PJ64_PRESENT_HINT * hint)
{
	if (hint != nullptr)
		s_present.Set(*hint);
}

extern "C" __attribute__((visibility("default"))) void PJ64GetPresentStatus(PJ64_PRESENT_STATUS * status)
{
	if (status != nullptr)
		s_present.FillRecorded(*status);
}
