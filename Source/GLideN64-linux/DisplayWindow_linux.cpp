// Graphics/OpenGLContext/windows/windows_DisplayWindow.cpp for Linux. The Windows version moves
// and resizes the emulator's window itself and switches the display mode for fullscreen; here the
// window is the render widget of the GUI, which the GUI sizes (and makes fullscreen), so the
// display window follows the drawable of that window and ChangeWindow leaves the window to the GUI.
#include <windows/GLideN64_Windows.h>
#include <GLideN64.h>
#include <Config.h>
#include <N64.h>
#include <RSP.h>
#include <FrameBuffer.h>
#include <GLideNUI/GLideNUI.h>
#include <Graphics/Context.h>
#include <Graphics/Parameters.h>
#include <DisplayWindow.h>
#include <windows/ScreenShot.h>
#include <Graphics/OpenGLContext/ThreadedOpenGl/opengl_Wrapper.h>
#include <Graphics/OpenGLContext/windows/WindowsWGL.h>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QWidget>

using namespace opengl;

namespace {
// the render widget behind the window of the emulator, found where the GUI creates it
QPointer<QWidget> g_renderWidget;
}

void setRenderWindow(void * _native)
{
	g_renderWidget = nullptr;
	if (_native == nullptr || QCoreApplication::instance() == nullptr)
		return;
	if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
		g_renderWidget = QWidget::find(reinterpret_cast<WId>(_native));
		return;
	}
	// the plugin is started by the emulation thread when it is changed while a game runs: widgets are
	// found by the thread that owns them (the same blocking call that gives the plugin the window handle)
	QWidget * found = nullptr;
	QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() { found = QWidget::find(reinterpret_cast<WId>(_native)); },
		Qt::BlockingQueuedConnection);
	g_renderWidget = found;
}

class DisplayWindowLinux : public DisplayWindow
{
public:
	DisplayWindowLinux() = default;

private:
	// the windowed resolution of the settings is asked of the GUI that owns the render window
	void _requestWindowSize();
	bool m_requestSize = false;
	// the frame is read from the back buffer just before it is shown: what the front buffer holds
	// after a swap is up to the driver (a cleared window on Mesa's software rasteriser)
	bool m_shotPending = false;
	void _captureBackBuffer();

	bool _start() override;
	void _stop() override;
	void _restart() override;
	void _swapBuffers() override;
	void _saveScreenshot() override;
	void _saveBufferContent(graphics::ObjectHandle _fbo, CachedTexture *_pTexture) override;
	bool _resizeWindow() override;
	void _changeWindow() override;
	void _readScreen(void **_pDest, long *_pWidth, long *_pHeight) override;
	void _readScreen2(void * _dest, int * _width, int * _height, int _front) override {}
	graphics::ObjectHandle _getDefaultFramebuffer() override;
};

DisplayWindow & DisplayWindow::get()
{
	static DisplayWindowLinux video;
	return video;
}

bool DisplayWindowLinux::_start()
{
	FunctionWrapper::setThreadedMode(config.video.threadedVideo);

	if (!FunctionWrapper::windowsStart())
		return false;
	m_requestSize = true;
	return _resizeWindow();
}

void DisplayWindowLinux::_stop()
{
	FunctionWrapper::windowsStop();
}

void DisplayWindowLinux::_restart()
{
	m_requestSize = true;
}

// SetWindowPos of the Windows display window: the window of the emulator is a Qt widget here, whose
// main window has SetRenderSize (when it is not there nothing asks and the window keeps its size)
void DisplayWindowLinux::_requestWindowSize()
{
	void * native = WindowsWGL::nativeWindow();
	if (native == nullptr || m_bFullscreen)
		return;
	const int width = config.video.windowedWidth;
	const int height = config.video.windowedHeight;
	QWidget * render = g_renderWidget;
	if (render == nullptr || render->window() == nullptr)
		return;
	QMetaObject::invokeMethod(render->window(), "SetRenderSize", Qt::QueuedConnection,
		Q_ARG(int, width), Q_ARG(int, height));
}

void DisplayWindowLinux::_swapBuffers()
{
	if (m_shotPending) {
		m_shotPending = false;
		_captureBackBuffer();
	}
	//Don't let the command queue grow too big buy waiting on no more swap buffers being queued
	FunctionWrapper::WaitForSwapBuffersQueued();

	FunctionWrapper::windowsSwapBuffers();

	// the GUI resized the window: the next frame is drawn at the new size
	int width = 0, height = 0;
	if (WindowsWGL::drawableSize(width, height) &&
		(static_cast<u32>(width) != m_screenWidth || static_cast<u32>(height) != m_screenHeight)) {
		m_resizeWidth = width;
		m_resizeHeight = height;
		m_bResizeWindow = true;
	}
}

void DisplayWindowLinux::_saveScreenshot()
{
	m_shotPending = true;
}

void DisplayWindowLinux::_captureBackBuffer()
{
	unsigned char * pixelData = NULL;
	GLint oldMode;
	glGetIntegerv(GL_READ_BUFFER, &oldMode);
	gfxContext.bindFramebuffer(graphics::bufferTarget::READ_FRAMEBUFFER, graphics::ObjectHandle::defaultFramebuffer);
	glReadBuffer(GL_BACK);
	pixelData = (unsigned char*)malloc(m_screenWidth * m_screenHeight * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, m_heightOffset, m_screenWidth, m_screenHeight, GL_RGB, GL_UNSIGNED_BYTE, pixelData);
	if (graphics::BufferAttachmentParam(oldMode) == graphics::bufferAttachment::COLOR_ATTACHMENT0) {
		FrameBuffer * pBuffer = frameBufferList().getCurrent();
		if (pBuffer != nullptr)
			gfxContext.bindFramebuffer(graphics::bufferTarget::READ_FRAMEBUFFER, pBuffer->m_FBO);
	}
	glReadBuffer(oldMode);
	SaveScreenshot(m_strScreenDirectory, RSP.romname, m_screenWidth, m_screenHeight, pixelData);
	free( pixelData );
}

void DisplayWindowLinux::_saveBufferContent(graphics::ObjectHandle _fbo, CachedTexture *_pTexture)
{
	unsigned char * pixelData = NULL;
	GLint oldMode;
	glGetIntegerv(GL_READ_BUFFER, &oldMode);
	gfxContext.bindFramebuffer(graphics::bufferTarget::READ_FRAMEBUFFER, _fbo);
	pixelData = (unsigned char*)malloc(_pTexture->width * _pTexture->height * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, _pTexture->width, _pTexture->height, GL_RGB, GL_UNSIGNED_BYTE, pixelData);
	if (graphics::BufferAttachmentParam(oldMode) == graphics::bufferAttachment::COLOR_ATTACHMENT0) {
		FrameBuffer * pCurrentBuffer = frameBufferList().getCurrent();
		if (pCurrentBuffer != nullptr)
			gfxContext.bindFramebuffer(graphics::bufferTarget::READ_FRAMEBUFFER, pCurrentBuffer->m_FBO);
	}
	glReadBuffer(oldMode);
	SaveScreenshot(m_strScreenDirectory, RSP.romname, _pTexture->width, _pTexture->height, pixelData);
	free(pixelData);
}

// fullscreen is the GUI's: it makes the render window fullscreen and the drawable follows
void DisplayWindowLinux::_changeWindow()
{
}

bool DisplayWindowLinux::_resizeWindow()
{
	if (m_requestSize) {
		m_requestSize = false;
		_requestWindowSize();
	}
	int width = 0, height = 0;
	if (m_resizeWidth != 0 && m_resizeHeight != 0) {
		width = m_resizeWidth;
		height = m_resizeHeight;
	} else if (!WindowsWGL::drawableSize(width, height)) {
		width = config.video.windowedWidth;
		height = config.video.windowedHeight;
	}
	m_screenWidth = width;
	m_screenHeight = height;
	m_heightOffset = 0;
	m_resizeWidth = m_resizeHeight = 0;
	_setBufferSize();
	return true;
}

void DisplayWindowLinux::_readScreen(void **_pDest, long *_pWidth, long *_pHeight)
{
	*_pWidth = m_width;
	*_pHeight = m_height;

	*_pDest = malloc(m_height * m_width * 3);
	if (*_pDest == nullptr)
		return;

	GLint oldMode;
	glGetIntegerv(GL_READ_BUFFER, &oldMode);
	gfxContext.bindFramebuffer(graphics::bufferTarget::READ_FRAMEBUFFER, graphics::ObjectHandle::defaultFramebuffer);
	glReadBuffer(GL_FRONT);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, m_heightOffset, m_width, m_height, GL_BGR, GL_UNSIGNED_BYTE, *_pDest);
	if (graphics::BufferAttachmentParam(oldMode) == graphics::bufferAttachment::COLOR_ATTACHMENT0) {
		FrameBuffer * pBuffer = frameBufferList().getCurrent();
		if (pBuffer != nullptr)
			gfxContext.bindFramebuffer(graphics::bufferTarget::READ_FRAMEBUFFER, pBuffer->m_FBO);
	}
	glReadBuffer(oldMode);
}

graphics::ObjectHandle DisplayWindowLinux::_getDefaultFramebuffer()
{
	return graphics::ObjectHandle::null;
}
