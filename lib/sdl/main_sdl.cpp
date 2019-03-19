/*
	This file is part of Warzone 2100.
	Copyright (C) 2013-2017  Warzone 2100 Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/**
 * @file main_sdl.cpp
 *
 * SDL backend code
 */

// Get platform defines before checking for them!
#include "lib/framework/wzapp.h"

#include <QtWidgets/QApplication>
// This is for the cross-compiler, for static QT 5 builds to avoid the 'plugins' crap on windows
#if defined(QT_STATICPLUGIN)
#include <QtCore/QtPlugin>
#endif
#include "lib/framework/input.h"
#include "lib/framework/utf.h"
#include "lib/framework/opengl.h"
#include "lib/ivis_opengl/pieclip.h"
#include "lib/ivis_opengl/piemode.h"
#include "lib/ivis_opengl/screen.h"
#include "lib/gamelib/gtime.h"
#include "src/warzoneconfig.h"
#include "src/game.h"
#include <SDL.h>
#include <SDL_thread.h>
#include <SDL_clipboard.h>
#include "wz2100icon.h"
#include "cursors_sdl.h"
#include <algorithm>
#include <map>
#include <locale.h>
#include <atomic>

// This is for the cross-compiler, for static QT 5 builds to avoid the 'plugins' crap on windows
#if defined(QT_STATICPLUGIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

void mainLoop();
// used in crash reports & version info
const char *BACKEND = "SDL";

std::map<KEY_CODE, SDL_Keycode> KEY_CODE_to_SDLKey;
std::map<SDL_Keycode, KEY_CODE > SDLKey_to_KEY_CODE;

// At this time, we only have 1 window and 1 GL context.
static SDL_Window *WZwindow = nullptr;

// The logical resolution of the game in the game's coordinate system (points).
unsigned int screenWidth = 0;
unsigned int screenHeight = 0;
// The logical resolution of the SDL window in the window's coordinate system (points) - i.e. not accounting for the Game Display Scale setting.
unsigned int windowWidth = 0;
unsigned int windowHeight = 0;
// The current display scale factor.
unsigned int current_displayScale = 100;
float current_displayScaleFactor = 1.f;

static std::vector<screeninfo> displaylist;	// holds all our possible display lists

QCoreApplication *appPtr;				// Needed for qtscript

const SDL_WindowFlags WZ_SDL_FULLSCREEN_MODE = SDL_WINDOW_FULLSCREEN;

std::atomic<Uint32> wzSDLAppEvent((Uint32)-1);
enum wzSDLAppEventCodes
{
	MAINTHREADEXEC
};

/* The possible states for keys */
enum KEY_STATE
{
 KEY_UP = 0,
	KEY_PRESSED = 1,
	KEY_DOWN = 2,
	KEY_RELEASED = 3,
	KEY_PRESSRELEASE = 4,	// When a key goes up and down in a frame
	KEY_DOUBLECLICK = 5,	// Only used by mouse keys
	KEY_DRAG = 6			// Only used by mouse keys
};

struct INPUT_STATE
{
	KEY_STATE state; /// Last key/mouse state
	UDWORD lastdown; /// last key/mouse button down timestamp
	Vector2i pressPos;    ///< Location of last mouse press event.
	Vector2i releasePos;  ///< Location of last mouse release event.
};

// Clipboard routines
bool has_scrap(void);
bool put_scrap(char *src);
bool get_scrap(char **dst);

/// constant for the interval between 2 singleclicks for doubleclick event in ms
#define DOUBLE_CLICK_INTERVAL 250

/* The current state of the keyboard */
static INPUT_STATE aKeyState[KEY_MAXSCAN];		// NOTE: SDL_NUM_SCANCODES is the max, but KEY_MAXSCAN is our limit

/* The current location of the mouse */
static Uint16 mouseXPos = 0;
static Uint16 mouseYPos = 0;
static bool mouseInWindow = true;

/* How far the mouse has to move to start a drag */
#define DRAG_THRESHOLD	5

/* Which button is being used for a drag */
static MOUSE_KEY_CODE dragKey;

/* The start of a possible drag by the mouse */
static int dragX = 0;
static int dragY = 0;

/* The current mouse button state */
static INPUT_STATE aMouseState[MOUSE_END];
static MousePresses mousePresses;

/* The current screen resizing state for this iteration through the game loop, in the game coordinate system */
struct ScreenSizeChange
{
	ScreenSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
	:	oldWidth(oldWidth)
	,	oldHeight(oldHeight)
	,	newWidth(newWidth)
	,	newHeight(newHeight)
	{ }
	unsigned int oldWidth;
	unsigned int oldHeight;
	unsigned int newWidth;
	unsigned int newHeight;
};
static ScreenSizeChange* currentScreenResizingStatus = nullptr;

/* The size of the input buffer */
#define INPUT_MAXSTR 256

/* The input string buffer */
struct InputKey
{
	UDWORD key;
	utf_32_char unicode;
};

static InputKey	pInputBuffer[INPUT_MAXSTR];
static InputKey	*pStartBuffer, *pEndBuffer;
static unsigned int CurrentKey = 0;			// Our Current keypress
bool GetTextEvents = false;
/**************************/
/***     Misc support   ***/
/**************************/

// See if we have TEXT in the clipboard
bool has_scrap(void)
{
	return SDL_HasClipboardText();
}

// When (if?) we decide to put text into the clipboard...
bool put_scrap(char *src)
{
	if (SDL_SetClipboardText(src))
	{
		debug(LOG_ERROR, "Could not put clipboard text because : %s", SDL_GetError());
		return false;
	}
	return true;
}

// Get text from the clipboard
bool get_scrap(char **dst)
{
	if (has_scrap())
	{
		char *cliptext = SDL_GetClipboardText();
		if (!cliptext)
		{
			debug(LOG_ERROR, "Could not get clipboard text because : %s", SDL_GetError());
			return false;
		}
		*dst = cliptext;
		return true;
	}
	else
	{
		// wasn't text or no text in the clipboard
		return false;
	}
}

void StartTextInput()
{
	if (!GetTextEvents)
	{
		SDL_StartTextInput();	// enable text events
		CurrentKey = 0;
		GetTextEvents = true;
		debug(LOG_INPUT, "SDL text events started");
	}
}

void StopTextInput()
{
	SDL_StopTextInput();	// disable text events
	CurrentKey = 0;
	GetTextEvents = false;
	debug(LOG_INPUT, "SDL text events stopped");
}

/* Put a character into a text buffer overwriting any text under the cursor */
WzString wzGetSelection()
{
	WzString retval;
	static char *scrap = nullptr;

	if (get_scrap(&scrap))
	{
		retval = WzString::fromUtf8(scrap);
	}
	return retval;
}

// Here we handle VSYNC enable/disabling
#if defined(WZ_WS_X11)

#if defined(__clang__)
// Some versions of the X11 headers (which are included by <GL/glx.h>)
// trigger `named variadic macros are a GNU extension [-Wvariadic-macros]`
// on Clang. https://lists.x.org/archives/xorg-devel/2015-January/045216.html
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wvariadic-macros"
#endif

#include <GL/glx.h> // GLXDrawable

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

// X11 polution
#ifdef Status
#undef Status
#endif // Status
#ifdef CursorShape
#undef CursorShape
#endif // CursorShape
#ifdef Bool
#undef Bool
#endif // Bool

#ifndef GLX_SWAP_INTERVAL_EXT
#define GLX_SWAP_INTERVAL_EXT 0x20F1
#endif // GLX_SWAP_INTERVAL_EXT

// Need this global for use case of only having glXSwapIntervalSGI
static int swapInterval = -1;

void wzSetSwapInterval(int interval)
{
	typedef void (* PFNGLXQUERYDRAWABLEPROC)(Display *, GLXDrawable, int, unsigned int *);
	typedef void (* PFNGLXSWAPINTERVALEXTPROC)(Display *, GLXDrawable, int);
	typedef int (* PFNGLXGETSWAPINTERVALMESAPROC)(void);
	typedef int (* PFNGLXSWAPINTERVALMESAPROC)(unsigned);
	typedef int (* PFNGLXSWAPINTERVALSGIPROC)(int);
	PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT;
	PFNGLXQUERYDRAWABLEPROC glXQueryDrawable;
	PFNGLXGETSWAPINTERVALMESAPROC glXGetSwapIntervalMESA;
	PFNGLXSWAPINTERVALMESAPROC glXSwapIntervalMESA;
	PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;

	if (interval < 0)
	{
		interval = 0;
	}

#if GLX_VERSION_1_2
	// Hack-ish, but better than not supporting GLX_SWAP_INTERVAL_EXT?
	GLXDrawable drawable = glXGetCurrentDrawable();
	Display *display =  glXGetCurrentDisplay();
	glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC) SDL_GL_GetProcAddress("glXSwapIntervalEXT");
	glXQueryDrawable = (PFNGLXQUERYDRAWABLEPROC) SDL_GL_GetProcAddress("glXQueryDrawable");

	if (glXSwapIntervalEXT && glXQueryDrawable && drawable)
	{
		unsigned clampedInterval;
		glXSwapIntervalEXT(display, drawable, interval);
		glXQueryDrawable(display, drawable, GLX_SWAP_INTERVAL_EXT, &clampedInterval);
		swapInterval = clampedInterval;
		return;
	}
#endif

	glXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC) SDL_GL_GetProcAddress("glXSwapIntervalMESA");
	glXGetSwapIntervalMESA = (PFNGLXGETSWAPINTERVALMESAPROC) SDL_GL_GetProcAddress("glXGetSwapIntervalMESA");
	if (glXSwapIntervalMESA && glXGetSwapIntervalMESA)
	{
		glXSwapIntervalMESA(interval);
		swapInterval = glXGetSwapIntervalMESA();
		return;
	}

	glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC) SDL_GL_GetProcAddress("glXSwapIntervalSGI");
	if (glXSwapIntervalSGI)
	{
		if (interval < 1)
		{
			interval = 1;
		}
		if (glXSwapIntervalSGI(interval))
		{
			// Error, revert to default
			swapInterval = 1;
			glXSwapIntervalSGI(1);
		}
		else
		{
			swapInterval = interval;
		}
		return;
	}
	swapInterval = 0;
}

int wzGetSwapInterval()
{
	if (swapInterval >= 0)
	{
		return swapInterval;
	}

	typedef void (* PFNGLXQUERYDRAWABLEPROC)(Display *, GLXDrawable, int, unsigned int *);
	typedef int (* PFNGLXGETSWAPINTERVALMESAPROC)(void);
	typedef int (* PFNGLXSWAPINTERVALSGIPROC)(int);
	PFNGLXQUERYDRAWABLEPROC glXQueryDrawable;
	PFNGLXGETSWAPINTERVALMESAPROC glXGetSwapIntervalMESA;
	PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;

#if GLX_VERSION_1_2
	// Hack-ish, but better than not supporting GLX_SWAP_INTERVAL_EXT?
	GLXDrawable drawable = glXGetCurrentDrawable();
	Display *display =  glXGetCurrentDisplay();
	glXQueryDrawable = (PFNGLXQUERYDRAWABLEPROC) SDL_GL_GetProcAddress("glXQueryDrawable");

	if (glXQueryDrawable && drawable)
	{
		unsigned interval;
		glXQueryDrawable(display, drawable, GLX_SWAP_INTERVAL_EXT, &interval);
		swapInterval = interval;
		return swapInterval;
	}
#endif

	glXGetSwapIntervalMESA = (PFNGLXGETSWAPINTERVALMESAPROC) SDL_GL_GetProcAddress("glXGetSwapIntervalMESA");
	if (glXGetSwapIntervalMESA)
	{
		swapInterval = glXGetSwapIntervalMESA();
		return swapInterval;
	}

	glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC) SDL_GL_GetProcAddress("glXSwapIntervalSGI");
	if (glXSwapIntervalSGI)
	{
		swapInterval = 1;
	}
	else
	{
		swapInterval = 0;
	}
	return swapInterval;
}

#elif !defined(WZ_OS_MAC)
// FIXME:  This can't be right?
void wzSetSwapInterval(int)
{
	return;
}

int wzGetSwapInterval()
{
	return 0;
}

#endif

std::vector<screeninfo> wzAvailableResolutions()
{
	return displaylist;
}

std::vector<unsigned int> wzAvailableDisplayScales()
{
	static const unsigned int wzDisplayScales[] = { 100, 125, 150, 200, 250, 300, 400, 500 };
	return std::vector<unsigned int>(wzDisplayScales, wzDisplayScales + (sizeof(wzDisplayScales) / sizeof(wzDisplayScales[0])));
}

void setDisplayScale(unsigned int displayScale)
{
	current_displayScale = displayScale;
	current_displayScaleFactor = (float)displayScale / 100.f;
}

unsigned int wzGetCurrentDisplayScale()
{
	return current_displayScale;
}

void wzShowMouse(bool visible)
{
	SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

int wzGetTicks()
{
	return SDL_GetTicks();
}

void wzFatalDialog(const char *msg)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "We have a problem!", msg, nullptr);
}

void wzScreenFlip()
{
	SDL_GL_SwapWindow(WZwindow);
}

void wzToggleFullscreen()
{
	Uint32 flags = SDL_GetWindowFlags(WZwindow);
	if (flags & WZ_SDL_FULLSCREEN_MODE)
	{
		SDL_SetWindowFullscreen(WZwindow, 0);
		wzSetWindowIsResizable(true);
	}
	else
	{
		SDL_SetWindowFullscreen(WZwindow, WZ_SDL_FULLSCREEN_MODE);
		wzSetWindowIsResizable(false);
	}
}

bool wzIsFullscreen()
{
	assert(WZwindow != nullptr);
	Uint32 flags = SDL_GetWindowFlags(WZwindow);
	if ((flags & SDL_WINDOW_FULLSCREEN) || (flags & SDL_WINDOW_FULLSCREEN_DESKTOP))
	{
		return true;
	}
	return false;
}

bool wzIsMaximized()
{
	assert(WZwindow != nullptr);
	Uint32 flags = SDL_GetWindowFlags(WZwindow);
	if (flags & SDL_WINDOW_MAXIMIZED)
	{
		return true;
	}
	return false;
}

void wzQuit()
{
	// Create a quit event to halt game loop.
	SDL_Event quitEvent;
	quitEvent.type = SDL_QUIT;
	SDL_PushEvent(&quitEvent);
}

void wzGrabMouse()
{
	SDL_SetWindowGrab(WZwindow, SDL_TRUE);
}

void wzReleaseMouse()
{
	SDL_SetWindowGrab(WZwindow, SDL_FALSE);
}

void wzDelay(unsigned int delay)
{
	SDL_Delay(delay);
}

/**************************/
/***    Thread support  ***/
/**************************/
WZ_THREAD *wzThreadCreate(int (*threadFunc)(void *), void *data)
{
	return (WZ_THREAD *)SDL_CreateThread(threadFunc, "wzThread", data);
}

int wzThreadJoin(WZ_THREAD *thread)
{
	int result;
	SDL_WaitThread((SDL_Thread *)thread, &result);
	return result;
}

void wzThreadDetach(WZ_THREAD *thread)
{
	SDL_DetachThread((SDL_Thread *)thread);
}

void wzThreadStart(WZ_THREAD *thread)
{
	(void)thread; // no-op
}

void wzYieldCurrentThread()
{
	SDL_Delay(40);
}

WZ_MUTEX *wzMutexCreate()
{
	return (WZ_MUTEX *)SDL_CreateMutex();
}

void wzMutexDestroy(WZ_MUTEX *mutex)
{
	SDL_DestroyMutex((SDL_mutex *)mutex);
}

void wzMutexLock(WZ_MUTEX *mutex)
{
	SDL_LockMutex((SDL_mutex *)mutex);
}

void wzMutexUnlock(WZ_MUTEX *mutex)
{
	SDL_UnlockMutex((SDL_mutex *)mutex);
}

WZ_SEMAPHORE *wzSemaphoreCreate(int startValue)
{
	return (WZ_SEMAPHORE *)SDL_CreateSemaphore(startValue);
}

void wzSemaphoreDestroy(WZ_SEMAPHORE *semaphore)
{
	SDL_DestroySemaphore((SDL_sem *)semaphore);
}

void wzSemaphoreWait(WZ_SEMAPHORE *semaphore)
{
	SDL_SemWait((SDL_sem *)semaphore);
}

void wzSemaphorePost(WZ_SEMAPHORE *semaphore)
{
	SDL_SemPost((SDL_sem *)semaphore);
}

// Asynchronously executes exec->doExecOnMainThread() on the main thread
// `exec` should be a subclass of `WZ_MAINTHREADEXEC`
//
// `exec` must be allocated on the heap since the main event loop takes ownership of it
// and will handle deleting it once it has been processed.
// It is not safe to access `exec` after calling wzAsyncExecOnMainThread.
//
// No guarantees are made about when execFunc() will be called relative to the
// calling of this function - this function may return before, during, or after
// execFunc()'s execution on the main thread.
void wzAsyncExecOnMainThread(WZ_MAINTHREADEXEC *exec)
{
	assert(exec != nullptr);
	Uint32 _wzSDLAppEvent = wzSDLAppEvent.load();
	assert(_wzSDLAppEvent != ((Uint32)-1));
	if (_wzSDLAppEvent == ((Uint32)-1)) {
		// The app-defined event has not yet been registered with SDL
		return;
	}
	SDL_Event event;
	SDL_memset(&event, 0, sizeof(event));
	event.type = _wzSDLAppEvent;
	event.user.code = wzSDLAppEventCodes::MAINTHREADEXEC;
	event.user.data1 = exec;
	event.user.data2 = 0;
	SDL_PushEvent(&event);
	// receiver handles deleting `exec` on the main thread after doExecOnMainThread() has been called
}

/*!
** The keycodes we care about
**/
static inline void initKeycodes()
{
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_ESC, SDLK_ESCAPE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_1, SDLK_1));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_2, SDLK_2));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_3, SDLK_3));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_4, SDLK_4));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_5, SDLK_5));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_6, SDLK_6));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_7, SDLK_7));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_8, SDLK_8));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_9, SDLK_9));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_0, SDLK_0));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_MINUS, SDLK_MINUS));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_EQUALS, SDLK_EQUALS));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_BACKSPACE, SDLK_BACKSPACE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_TAB, SDLK_TAB));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_Q, SDLK_q));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_W, SDLK_w));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_E, SDLK_e));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_R, SDLK_r));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_T, SDLK_t));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_Y, SDLK_y));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_U, SDLK_u));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_I, SDLK_i));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_O, SDLK_o));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_P, SDLK_p));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LBRACE, SDLK_LEFTBRACKET));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RBRACE, SDLK_RIGHTBRACKET));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RETURN, SDLK_RETURN));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LCTRL, SDLK_LCTRL));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_A, SDLK_a));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_S, SDLK_s));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_D, SDLK_d));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F, SDLK_f));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_G, SDLK_g));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_H, SDLK_h));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_J, SDLK_j));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_K, SDLK_k));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_L, SDLK_l));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_SEMICOLON, SDLK_SEMICOLON));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_QUOTE, SDLK_QUOTE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_BACKQUOTE, SDLK_BACKQUOTE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LSHIFT, SDLK_LSHIFT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LMETA, SDLK_LGUI));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LSUPER, SDLK_LGUI));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_BACKSLASH, SDLK_BACKSLASH));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_Z, SDLK_z));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_X, SDLK_x));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_C, SDLK_c));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_V, SDLK_v));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_B, SDLK_b));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_N, SDLK_n));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_M, SDLK_m));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_COMMA, SDLK_COMMA));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_FULLSTOP, SDLK_PERIOD));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_FORWARDSLASH, SDLK_SLASH));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RSHIFT, SDLK_RSHIFT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RMETA, SDLK_RGUI));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RSUPER, SDLK_RGUI));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_STAR, SDLK_KP_MULTIPLY));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LALT, SDLK_LALT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_SPACE, SDLK_SPACE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_CAPSLOCK, SDLK_CAPSLOCK));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F1, SDLK_F1));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F2, SDLK_F2));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F3, SDLK_F3));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F4, SDLK_F4));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F5, SDLK_F5));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F6, SDLK_F6));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F7, SDLK_F7));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F8, SDLK_F8));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F9, SDLK_F9));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F10, SDLK_F10));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_NUMLOCK, SDLK_NUMLOCKCLEAR));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_SCROLLLOCK, SDLK_SCROLLLOCK));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_7, SDLK_KP_7));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_8, SDLK_KP_8));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_9, SDLK_KP_9));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_MINUS, SDLK_KP_MINUS));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_4, SDLK_KP_4));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_5, SDLK_KP_5));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_6, SDLK_KP_6));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_PLUS, SDLK_KP_PLUS));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_1, SDLK_KP_1));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_2, SDLK_KP_2));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_3, SDLK_KP_3));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_0, SDLK_KP_0));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_FULLSTOP, SDLK_KP_PERIOD));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F11, SDLK_F11));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_F12, SDLK_F12));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RCTRL, SDLK_RCTRL));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KP_BACKSLASH, SDLK_KP_DIVIDE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RALT, SDLK_RALT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_HOME, SDLK_HOME));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_UPARROW, SDLK_UP));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_PAGEUP, SDLK_PAGEUP));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_LEFTARROW, SDLK_LEFT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_RIGHTARROW, SDLK_RIGHT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_END, SDLK_END));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_DOWNARROW, SDLK_DOWN));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_PAGEDOWN, SDLK_PAGEDOWN));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_INSERT, SDLK_INSERT));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_DELETE, SDLK_DELETE));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_KPENTER, SDLK_KP_ENTER));
	KEY_CODE_to_SDLKey.insert(std::pair<KEY_CODE, SDL_Keycode>(KEY_IGNORE, SDL_Keycode(5190)));

	std::map<KEY_CODE, SDL_Keycode>::iterator it;
	for (it = KEY_CODE_to_SDLKey.begin(); it != KEY_CODE_to_SDLKey.end(); it++)
	{
		SDLKey_to_KEY_CODE.insert(std::pair<SDL_Keycode, KEY_CODE>(it->second, it->first));
	}
}

int sdlKeyToKeyCode(int key)
{
	std::map<SDL_Keycode, KEY_CODE >::iterator it;

	it = SDLKey_to_KEY_CODE.find((SDL_Keycode) key);
	if (it != SDLKey_to_KEY_CODE.end())
	{
		return it->second;
	}
	return (KEY_CODE)key;
}

static inline SDL_Keycode keyCodeToSDLKey(KEY_CODE code)
{
	std::map<KEY_CODE, SDL_Keycode>::iterator it;

	it = KEY_CODE_to_SDLKey.find(code);
	if (it != KEY_CODE_to_SDLKey.end())
	{
		return it->second;
	}
	return (SDL_Keycode)code;
}

// Cyclic increment.
static InputKey *inputPointerNext(InputKey *p)
{
	return p + 1 == pInputBuffer + INPUT_MAXSTR ? pInputBuffer : p + 1;
}

/* add count copies of the characater code to the input buffer */
void inputAddBuffer(UDWORD key, int unicode)
{
	/* Calculate what pEndBuffer will be set to next */
	InputKey	*pNext = inputPointerNext(pEndBuffer);

	if (pNext == pStartBuffer)
	{
		return;	// Buffer full.
	}

	// Add key to buffer.
	pEndBuffer->key = key;
	pEndBuffer->unicode = (utf_32_char) unicode;
	pEndBuffer = pNext;
}

void keyScanToString(KEY_CODE code, char *ascii, UDWORD maxStringSize)
{
	if (code == KEY_LCTRL)
	{
		// shortcuts with modifier keys work with either key.
		strcpy(ascii, "Ctrl");
		return;
	}
	else if (code == KEY_LSHIFT)
	{
		// shortcuts with modifier keys work with either key.
		strcpy(ascii, "Shift");
		return;
	}
	else if (code == KEY_LALT)
	{
		// shortcuts with modifier keys work with either key.
		strcpy(ascii, "Alt");
		return;
	}
	else if (code == KEY_LMETA)
	{
		// shortcuts with modifier keys work with either key.
#ifdef WZ_OS_MAC
		strcpy(ascii, "Cmd");
#else
		strcpy(ascii, "Meta");
#endif
		return;
	}

	if (code < KEY_MAXSCAN)
	{
		snprintf(ascii, maxStringSize, "%s", SDL_GetKeyName(keyCodeToSDLKey(code)));
		if (ascii[0] >= 'a' && ascii[0] <= 'z' && ascii[1] != 0)
		{
			// capitalize
			ascii[0] += 'A' - 'a';
			return;
		}
	}
	else
	{
		strcpy(ascii, "???");
	}
}

/* Initialise the input module */
void inputInitialise(void)
{
	for (unsigned int i = 0; i < KEY_MAXSCAN; i++)
	{
		aKeyState[i].state = KEY_UP;
	}

	for (unsigned int i = 0; i < MOUSE_END; i++)
	{
		aMouseState[i].state = KEY_UP;
	}

	pStartBuffer = pInputBuffer;
	pEndBuffer = pInputBuffer;

	dragX = mouseXPos = screenWidth / 2;
	dragY = mouseYPos = screenHeight / 2;
	dragKey = MOUSE_LMB;

}

/* Clear the input buffer */
void inputClearBuffer(void)
{
	pStartBuffer = pInputBuffer;
	pEndBuffer = pInputBuffer;
}

/* Return the next key press or 0 if no key in the buffer.
 * The key returned will have been remapped to the correct ascii code for the
 * windows key map.
 * All key presses are buffered up (including windows auto repeat).
 */
UDWORD inputGetKey(utf_32_char *unicode)
{
	UDWORD	 retVal;

	if (pStartBuffer == pEndBuffer)
	{
		return 0;	// Buffer empty.
	}

	retVal = pStartBuffer->key;
	if (unicode)
	{
		*unicode = pStartBuffer->unicode;
	}
	if (!retVal)
	{
		retVal = ' ';  // Don't return 0 if we got a virtual key, since that's interpreted as no input.
	}
	pStartBuffer = inputPointerNext(pStartBuffer);

	return retVal;
}

MousePresses const &inputGetClicks()
{
	return mousePresses;
}

/*!
 * This is called once a frame so that the system can tell
 * whether a key was pressed this turn or held down from the last frame.
 */
void inputNewFrame(void)
{
	// handle the keyboard
	for (unsigned int i = 0; i < KEY_MAXSCAN; i++)
	{
		if (aKeyState[i].state == KEY_PRESSED)
		{
			aKeyState[i].state = KEY_DOWN;
			debug(LOG_NEVER, "This key is DOWN! %x, %d [%s]", i, i, SDL_GetKeyName(keyCodeToSDLKey((KEY_CODE)i)));
		}
		else if (aKeyState[i].state == KEY_RELEASED  ||
		         aKeyState[i].state == KEY_PRESSRELEASE)
		{
			aKeyState[i].state = KEY_UP;
			debug(LOG_NEVER, "This key is UP! %x, %d [%s]", i, i, SDL_GetKeyName(keyCodeToSDLKey((KEY_CODE)i)));
		}
	}

	// handle the mouse
	for (unsigned int i = 0; i < MOUSE_END; i++)
	{
		if (aMouseState[i].state == KEY_PRESSED)
		{
			aMouseState[i].state = KEY_DOWN;
		}
		else if (aMouseState[i].state == KEY_RELEASED
		         || aMouseState[i].state == KEY_DOUBLECLICK
		         || aMouseState[i].state == KEY_PRESSRELEASE)
		{
			aMouseState[i].state = KEY_UP;
		}
	}
	mousePresses.clear();
}

/*!
 * Release all keys (and buttons) when we lose focus
 */
void inputLoseFocus(void)
{
	/* Lost the window focus, have to take this as a global key up */
	for (unsigned int i = 0; i < KEY_MAXSCAN; i++)
	{
		aKeyState[i].state = KEY_UP;
	}
	for (unsigned int i = 0; i < MOUSE_END; i++)
	{
		aMouseState[i].state = KEY_UP;
	}
}

/* This returns true if the key is currently depressed */
bool keyDown(KEY_CODE code)
{
	ASSERT_OR_RETURN(false, code < KEY_MAXSCAN, "Invalid keycode of %d!", (int)code);
	return (aKeyState[code].state != KEY_UP);
}

/* This returns true if the key went from being up to being down this frame */
bool keyPressed(KEY_CODE code)
{
	ASSERT_OR_RETURN(false, code < KEY_MAXSCAN, "Invalid keycode of %d!", (int)code);
	return ((aKeyState[code].state == KEY_PRESSED) || (aKeyState[code].state == KEY_PRESSRELEASE));
}

/* This returns true if the key went from being down to being up this frame */
bool keyReleased(KEY_CODE code)
{
	ASSERT_OR_RETURN(false, code < KEY_MAXSCAN, "Invalid keycode of %d!", (int)code);
	return ((aKeyState[code].state == KEY_RELEASED) || (aKeyState[code].state == KEY_PRESSRELEASE));
}

/* Return the X coordinate of the mouse */
Uint16 mouseX(void)
{
	return mouseXPos;
}

/* Return the Y coordinate of the mouse */
Uint16 mouseY(void)
{
	return mouseYPos;
}

void setMouseInWindow(bool x)
{
  mouseInWindow = x;
}

bool wzMouseInWindow()
{
	return mouseInWindow;
}

Vector2i mousePressPos_DEPRECATED(MOUSE_KEY_CODE code)
{
	return aMouseState[code].pressPos;
}

Vector2i mouseReleasePos_DEPRECATED(MOUSE_KEY_CODE code)
{
	return aMouseState[code].releasePos;
}

/* This returns true if the mouse key is currently depressed */
bool mouseDown(MOUSE_KEY_CODE code)
{
	return (aMouseState[code].state != KEY_UP) ||

	       // holding down LMB and RMB counts as holding down MMB
	       (code == MOUSE_MMB && aMouseState[MOUSE_LMB].state != KEY_UP && aMouseState[MOUSE_RMB].state != KEY_UP);
}

/* This returns true if the mouse key was double clicked */
bool mouseDClicked(MOUSE_KEY_CODE code)
{
	return (aMouseState[code].state == KEY_DOUBLECLICK);
}

/* This returns true if the mouse key went from being up to being down this frame */
bool mousePressed(MOUSE_KEY_CODE code)
{
	return ((aMouseState[code].state == KEY_PRESSED) ||
	        (aMouseState[code].state == KEY_DOUBLECLICK) ||
	        (aMouseState[code].state == KEY_PRESSRELEASE));
}

/* This returns true if the mouse key went from being down to being up this frame */
bool mouseReleased(MOUSE_KEY_CODE code)
{
	return ((aMouseState[code].state == KEY_RELEASED) ||
	        (aMouseState[code].state == KEY_DOUBLECLICK) ||
	        (aMouseState[code].state == KEY_PRESSRELEASE));
}

/* Check for a mouse drag, return the drag start coords if dragging */
bool mouseDrag(MOUSE_KEY_CODE code, UDWORD *px, UDWORD *py)
{
	if ((aMouseState[code].state == KEY_DRAG) ||
	    // dragging LMB and RMB counts as dragging MMB
	    (code == MOUSE_MMB && ((aMouseState[MOUSE_LMB].state == KEY_DRAG && aMouseState[MOUSE_RMB].state != KEY_UP) ||
	                           (aMouseState[MOUSE_LMB].state != KEY_UP && aMouseState[MOUSE_RMB].state == KEY_DRAG))))
	{
		*px = dragX;
		*py = dragY;
		return true;
	}

	return false;
}

int getSymKey(int keysym)
{
  int vk = 0;
  switch (keysym)
		{
      // our "editing" keys for text
		case SDLK_LEFT:
			vk = INPBUF_LEFT;
			break;
		case SDLK_RIGHT:
			vk = INPBUF_RIGHT;
			break;
		case SDLK_UP:
			vk = INPBUF_UP;
			break;
		case SDLK_DOWN:
			vk = INPBUF_DOWN;
			break;
		case SDLK_HOME:
			vk = INPBUF_HOME;
			break;
		case SDLK_END:
			vk = INPBUF_END;
			break;
		case SDLK_INSERT:
			vk = INPBUF_INS;
			break;
		case SDLK_DELETE:
			vk = INPBUF_DEL;
			break;
		case SDLK_PAGEUP:
			vk = INPBUF_PGUP;
			break;
		case SDLK_PAGEDOWN:
			vk = INPBUF_PGDN;
			break;
		case KEY_BACKSPACE:
			vk = INPBUF_BKSPACE;
			break;
		case KEY_TAB:
			vk = INPBUF_TAB;
			break;
		case	KEY_RETURN:
			vk = INPBUF_CR;
			break;
		case 	KEY_ESC:
			vk = INPBUF_ESC;
			break;
		default:
      vk = keysym;
			break;
		}
  // Keycodes without character representations are determined by their scancode bitwise OR-ed with 1<<30 (0x40000000).
  CurrentKey = vk; //FIXME Sideffect!

  // Take care of 'editing' keys that were pressed
  inputAddBuffer(vk, 0);
  return vk;
}

/*!
 * Handle keyboard events
 */
static void inputHandleKeyEvent(SDL_KeyboardEvent *keyEvent)
{
  int virtualKey = getSymKey(keyEvent->keysym.sym);
	UDWORD code = sdlKeyToKeyCode(virtualKey);
  if (code >= KEY_MAXSCAN)
		{
			return;
		}
  if(keyEvent->type == SDL_KEYDOWN)
	{
		if (aKeyState[code].state == KEY_UP ||
		    aKeyState[code].state == KEY_RELEASED ||
		    aKeyState[code].state == KEY_PRESSRELEASE)
		{
			// whether double key press or not
			aKeyState[code].state = KEY_PRESSED;
			aKeyState[code].lastdown = 0;
		}
  } else {
		if (aKeyState[code].state == KEY_PRESSED)
		{
			aKeyState[code].state = KEY_PRESSRELEASE;
		}
		else if (aKeyState[code].state == KEY_DOWN)
		{
			aKeyState[code].state = KEY_RELEASED;
		}
	}
}

int getKey (int code)
{
  return aKeyState[code].state;
}

void setKey (int code, int state)
{
  aKeyState[code].state = (KEY_STATE) state;
  if (state == (int) KEY_PRESSED) {
    aKeyState[code].lastdown = 0;
  }
}

/*!
 * Handle text events (if we were to use SDL2)
*/
void inputhandleText(SDL_TextInputEvent *Tevent)
{
	size_t *newtextsize = nullptr;
	int size = 	SDL_strlen(Tevent->text);
  utf_32_char *utf8Buf;				// is like the old 'unicode' from SDL 1.x
	if (size)
	{
		utf8Buf = UTF8toUTF32(Tevent->text, newtextsize);
		debug(LOG_INPUT, "Keyboard: text input \"%s\"", Tevent->text);
		inputAddBuffer(CurrentKey, *utf8Buf);
	}
}

/*!
 * Handle mouse wheel events
 */
static void inputHandleMouseWheelEvent(SDL_MouseWheelEvent *wheel)
{
	if (wheel->x > 0 || wheel->y > 0)
	{
		aMouseState[MOUSE_WUP].state = KEY_PRESSED;
		aMouseState[MOUSE_WUP].lastdown = 0;
	}
	else if (wheel->x < 0 || wheel->y < 0)
	{
		aMouseState[MOUSE_WDN].state = KEY_PRESSED;
		aMouseState[MOUSE_WDN].lastdown = 0;
	}
}

int getMouse (int key)
{
  return aMouseState[key].state;
}

void setMouse (int key, int state)
{
  aMouseState[key].state = (KEY_STATE) state;
}

void setKeyDown (int code)
{
  MOUSE_KEY_CODE mouseKeyCode = (MOUSE_KEY_CODE) code;
  // whether double click or not
  if (realTime - aMouseState[mouseKeyCode].lastdown < DOUBLE_CLICK_INTERVAL)
    {
      aMouseState[mouseKeyCode].state = KEY_DOUBLECLICK;
      aMouseState[mouseKeyCode].lastdown = 0;
    }
  else
    {
      aMouseState[mouseKeyCode].state = KEY_PRESSED;
      aMouseState[mouseKeyCode].lastdown = realTime;
    }

  if (mouseKeyCode < MOUSE_X1) // Assume they are draggin' with either LMB|RMB|MMB
    {
      dragKey = mouseKeyCode;
      dragX = mouseXPos;
      dragY = mouseYPos;
    }
}

void handleMouseTmp(MOUSE_KEY_CODE mouseKeyCode, int mouseXPos, int mouseYPos, bool down)
{
  printf("handleMouseTmp: %i %i %i %i\n", (int) mouseKeyCode, mouseXPos, mouseYPos, down);
	MousePress mousePress;
	mousePress.key = mouseKeyCode;
	mousePress.pos = Vector2i(mouseXPos, mouseYPos);

	if(down)
	{
		mousePress.action = MousePress::Press;
		mousePresses.push_back(mousePress);

		aMouseState[mouseKeyCode].pressPos.x = mouseXPos;
		aMouseState[mouseKeyCode].pressPos.y = mouseYPos;
		if (aMouseState[mouseKeyCode].state == KEY_UP
		    || aMouseState[mouseKeyCode].state == KEY_RELEASED
		    || aMouseState[mouseKeyCode].state == KEY_PRESSRELEASE)
		{
      setKeyDown(mouseKeyCode);
		}
  }
  else {
		mousePress.action = MousePress::Release;
		mousePresses.push_back(mousePress);

		aMouseState[mouseKeyCode].releasePos.x = mouseXPos;
		aMouseState[mouseKeyCode].releasePos.y = mouseYPos;
		if (aMouseState[mouseKeyCode].state == KEY_PRESSED)
		{
			aMouseState[mouseKeyCode].state = KEY_PRESSRELEASE;
		}
		else if (aMouseState[mouseKeyCode].state == KEY_DOWN
		         || aMouseState[mouseKeyCode].state == KEY_DRAG
		         || aMouseState[mouseKeyCode].state == KEY_DOUBLECLICK)
		{
			aMouseState[mouseKeyCode].state = KEY_RELEASED;
		}
	}
}

/*!
 * Handle mouse button events (We can handle up to 5)
 */
static void inputHandleMouseButtonEvent(SDL_MouseButtonEvent *buttonEvent)
{
	mouseXPos = (int)((float)buttonEvent->x / current_displayScaleFactor);
	mouseYPos = (int)((float)buttonEvent->y / current_displayScaleFactor);

	MOUSE_KEY_CODE mouseKeyCode;
	switch (buttonEvent->button)
	{
	case SDL_BUTTON_LEFT:
    mouseKeyCode = MOUSE_LMB;
    break;
	case SDL_BUTTON_MIDDLE:
    mouseKeyCode = MOUSE_MMB;
    break;
	case SDL_BUTTON_RIGHT:
    mouseKeyCode = MOUSE_RMB;
    break;
	case SDL_BUTTON_X1:
    mouseKeyCode = MOUSE_X1;
    break;
	case SDL_BUTTON_X2:
    mouseKeyCode = MOUSE_X2;
    break;
	default:
    return;
 // Unknown button.
	}
  handleMouseTmp(mouseKeyCode, mouseXPos, mouseYPos, buttonEvent->type == SDL_MOUSEBUTTONDOWN);
}


void setMousePos (int code, bool press, Vector2i pos)
{
  if (press) {
    aMouseState[code].pressPos.x = pos.x;
    aMouseState[code].pressPos.y = pos.y;
  }
  else {
    aMouseState[code].releasePos.x = pos.x;
    aMouseState[code].releasePos.y = pos.y;
  }
}

void pushMouses (MousePress mp)
{
  mousePresses.push_back(mp);
}

/*!
 * Handle mousemotion events
 */
void inputHandleMouseMotionEvent(int x, int y)
{
  /* store the current mouse position */
  mouseXPos = (int)((float)x / current_displayScaleFactor);
  mouseYPos = (int)((float)y / current_displayScaleFactor);

  /* now see if a drag has started */
  if ((aMouseState[dragKey].state == KEY_PRESSED ||
       aMouseState[dragKey].state == KEY_DOWN) &&
      (ABSDIF(dragX, mouseXPos) > DRAG_THRESHOLD ||
       ABSDIF(dragY, mouseYPos) > DRAG_THRESHOLD))
			aMouseState[dragKey].state = KEY_DRAG;
}

void setMouseX (int x)
{
  mouseXPos = x;
}

void setMouseY (int y)
{
  mouseYPos = y;
}

int getDragKey()
{
  return dragKey;
}

int getx()
{
  return dragX;
}

int gety()
{
  return dragY;
}

static int copied_argc = 0;
static char** copied_argv = nullptr;

// This stage, we only setup keycodes, and copy argc & argv for later use initializing Qt stuff for the script engine.
void wzMain()
{
	initKeycodes();
	appPtr = new QApplication(copied_argc, copied_argv);
}

#define MIN_WZ_GAMESCREEN_WIDTH 640
#define MIN_WZ_GAMESCREEN_HEIGHT 480

void handleGameScreenSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
{
	screenWidth = newWidth;
	screenHeight = newHeight;

	pie_SetVideoBufferWidth(screenWidth);
	pie_SetVideoBufferHeight(screenHeight);
	pie_UpdateSurfaceGeometry();
	screen_updateGeometry();

	if (currentScreenResizingStatus == nullptr)
	{
		// The screen size change details are stored in scaled, logical units (points)
		// i.e. the values expect by the game engine.
		currentScreenResizingStatus = new ScreenSizeChange(oldWidth, oldHeight, screenWidth, screenHeight);
	}
	else
	{
		// update the new screen width / height, in case more than one resize message is processed this event loop
		currentScreenResizingStatus->newWidth = screenWidth;
		currentScreenResizingStatus->newHeight = screenHeight;
	}
}

void handleWindowSizeChange(unsigned int oldWidth, unsigned int oldHeight, unsigned int newWidth, unsigned int newHeight)
{
  oldWidth = windowWidth;
  oldHeight = windowHeight;
	windowWidth = newWidth;
	windowHeight = newHeight;

	// NOTE: This function receives the window size in the window's logical units, but not accounting for the interface scale factor.
	// Therefore, the provided old/newWidth/Height must be divided by the interface scale factor to calculate the new
	// *game* screen logical width / height.
	unsigned int oldScreenWidth = oldWidth / current_displayScaleFactor;
	unsigned int oldScreenHeight = oldHeight / current_displayScaleFactor;
	unsigned int newScreenWidth = newWidth / current_displayScaleFactor;
	unsigned int newScreenHeight = newHeight / current_displayScaleFactor;

	handleGameScreenSizeChange(oldScreenWidth, oldScreenHeight, newScreenWidth, newScreenHeight);

  glUpdate();
	}

void glUpdate()
{
  // Update the viewport to use the new *drawable* size (which may be greater than the new window size
	// if SDL's built-in high-DPI support is enabled and functioning).
	int drawableWidth = 0, drawableHeight = 0;
	SDL_GL_GetDrawableSize(WZwindow, &drawableWidth, &drawableHeight);
	debug(LOG_WZ, "Logical Size: %d x %d; Drawable Size: %d x %d", screenWidth, screenHeight, drawableWidth, drawableHeight);
	glViewport(0, 0, drawableWidth, drawableHeight);
	glCullFace(GL_FRONT);
	glEnable(GL_CULL_FACE);
}


void wzGetMinimumWindowSizeForDisplayScaleFactor(unsigned int *minWindowWidth, unsigned int *minWindowHeight, float displayScaleFactor = current_displayScaleFactor)
{
	if (minWindowWidth != nullptr)
	{
		*minWindowWidth = (int)ceil(MIN_WZ_GAMESCREEN_WIDTH * displayScaleFactor);
	}
	if (minWindowHeight != nullptr)
	{
		*minWindowHeight = (int)ceil(MIN_WZ_GAMESCREEN_HEIGHT * displayScaleFactor);
	}
}

void wzGetMaximumDisplayScaleFactorsForWindowSize(unsigned int windowWidth, unsigned int windowHeight, float *horizScaleFactor, float *vertScaleFactor)
{
	if (horizScaleFactor != nullptr)
	{
		*horizScaleFactor = (float)windowWidth / (float)MIN_WZ_GAMESCREEN_WIDTH;
	}
	if (vertScaleFactor != nullptr)
	{
		*vertScaleFactor = (float)windowHeight / (float)MIN_WZ_GAMESCREEN_HEIGHT;
	}
}

float wzGetMaximumDisplayScaleFactorForWindowSize(unsigned int windowWidth, unsigned int windowHeight)
{
	float maxHorizScaleFactor = 0.f, maxVertScaleFactor = 0.f;
	wzGetMaximumDisplayScaleFactorsForWindowSize(windowWidth, windowHeight, &maxHorizScaleFactor, &maxVertScaleFactor);
	return std::min(maxHorizScaleFactor, maxVertScaleFactor);
}

// returns: the maximum display scale percentage (sourced from wzAvailableDisplayScales), or 0 if window is below the minimum required size for the minimum support display scale
unsigned int wzGetMaximumDisplayScaleForWindowSize(unsigned int windowWidth, unsigned int windowHeight)
{
	float maxDisplayScaleFactor = wzGetMaximumDisplayScaleFactorForWindowSize(windowWidth, windowHeight);
	unsigned int maxDisplayScalePercentage = floor(maxDisplayScaleFactor * 100.f);

	auto availableDisplayScales = wzAvailableDisplayScales();
	std::sort(availableDisplayScales.begin(), availableDisplayScales.end());

	auto maxDisplayScale = std::lower_bound(availableDisplayScales.begin(), availableDisplayScales.end(), maxDisplayScalePercentage);
	if (maxDisplayScale == availableDisplayScales.end())
	{
		return 0;
	}
	if (*maxDisplayScale != maxDisplayScalePercentage)
	{
		--maxDisplayScale;
	}
	return *maxDisplayScale;
}

bool wzWindowSizeIsSmallerThanMinimumRequired(unsigned int windowWidth, unsigned int windowHeight, float displayScaleFactor = current_displayScaleFactor)
{
	unsigned int minWindowWidth = 0, minWindowHeight = 0;
	wzGetMinimumWindowSizeForDisplayScaleFactor(&minWindowWidth, &minWindowHeight, displayScaleFactor);
	return ((windowWidth < minWindowWidth) || (windowHeight < minWindowHeight));
}

void processScreenSizeChangeNotificationIfNeeded()
{
	if (currentScreenResizingStatus != nullptr)
	{
		// WZ must process the screen size change
		gameScreenSizeDidChange(currentScreenResizingStatus->oldWidth, currentScreenResizingStatus->oldHeight, currentScreenResizingStatus->newWidth, currentScreenResizingStatus->newHeight);
		delete currentScreenResizingStatus;
		currentScreenResizingStatus = nullptr;
	}
}

bool wzChangeDisplayScale(unsigned int displayScale)
{
	float newDisplayScaleFactor = (float)displayScale / 100.f;

	if (wzWindowSizeIsSmallerThanMinimumRequired(windowWidth, windowHeight, newDisplayScaleFactor))
	{
		// The current window width and/or height are below the required minimum window size
		// for this display scale factor.
		return false;
	}

	// Store the new display scale factor
	setDisplayScale(displayScale);

	// Set the new minimum window size
	unsigned int minWindowWidth = 0, minWindowHeight = 0;
	wzGetMinimumWindowSizeForDisplayScaleFactor(&minWindowWidth, &minWindowHeight, newDisplayScaleFactor);
	SDL_SetWindowMinimumSize(WZwindow, minWindowWidth, minWindowHeight);

	// Update the game's logical screen size
	unsigned int oldScreenWidth = screenWidth, oldScreenHeight = screenHeight;
	unsigned int newScreenWidth = windowWidth, newScreenHeight = windowHeight;
	if (newDisplayScaleFactor > 1.0)
	{
		newScreenWidth = windowWidth / newDisplayScaleFactor;
		newScreenHeight = windowHeight / newDisplayScaleFactor;
	}
	handleGameScreenSizeChange(oldScreenWidth, oldScreenHeight, newScreenWidth, newScreenHeight);
	gameDisplayScaleFactorDidChange(newDisplayScaleFactor);

	// Update the current mouse coordinates
	// (The prior stored mouseXPos / mouseYPos apply to the old coordinate system, and must be translated to the
	// new game coordinate system. Since the mouse hasn't moved - or it would generate events that override this -
	// the current position with respect to the window (which hasn't changed size) can be queried and used to
	// calculate the new game coordinate system mouse position.)
	//
	int windowMouseXPos = 0, windowMouseYPos = 0;
	SDL_GetMouseState(&windowMouseXPos, &windowMouseYPos);
	debug(LOG_WZ, "Old mouse position: %d, %d", mouseXPos, mouseYPos);
	mouseXPos = (int)((float)windowMouseXPos / current_displayScaleFactor);
	mouseYPos = (int)((float)windowMouseYPos / current_displayScaleFactor);
	debug(LOG_WZ, "New mouse position: %d, %d", mouseXPos, mouseYPos);


	processScreenSizeChangeNotificationIfNeeded();

	return true;
}

bool wzChangeWindowResolution(int screen, unsigned int width, unsigned int height)
{
	assert(WZwindow != nullptr);
	debug(LOG_WZ, "Attempt to change resolution to [%d] %dx%d", screen, width, height);

	// Get current window size + position + bounds
	int prev_x = 0, prev_y = 0, prev_width = 0, prev_height = 0;
	SDL_GetWindowPosition(WZwindow, &prev_x, &prev_y);
	SDL_GetWindowSize(WZwindow, &prev_width, &prev_height);

	// Get the usable bounds for the current screen
	SDL_Rect bounds;
	if (wzIsFullscreen())
	{
		// When in fullscreen mode, obtain the screen's overall bounds
		if (SDL_GetDisplayBounds(screen, &bounds) != 0) {
			debug(LOG_ERROR, "Failed to get display bounds for screen: %d", screen);
			return false;
		}
		debug(LOG_WZ, "SDL_GetDisplayBounds for screen [%d]: pos %d x %d : res %d x %d", screen, (int)bounds.x, (int)bounds.y, (int)bounds.w, (int)bounds.h);
	}
	else
	{
		// When in windowed mode, obtain the screen's *usable* display bounds
		if (SDL_GetDisplayUsableBounds(screen, &bounds) != 0) {
			debug(LOG_ERROR, "Failed to get usable display bounds for screen: %d", screen);
			return false;
		}
		debug(LOG_WZ, "SDL_GetDisplayUsableBounds for screen [%d]: pos %d x %d : WxH %d x %d", screen, (int)bounds.x, (int)bounds.y, (int)bounds.w, (int)bounds.h);

		// Verify that the desired window size does not exceed the usable bounds of the specified display.
		if ((width > bounds.w) || (height > bounds.h))
		{
			debug(LOG_WZ, "Unable to change window size to (%d x %d) because it is larger than the screen's usable bounds", width, height);
			return false;
		}
	}

	// Check whether the desired window size is smaller than the minimum required for the current Display Scale
	unsigned int priorDisplayScale = current_displayScale;
	if (wzWindowSizeIsSmallerThanMinimumRequired(width, height))
	{
		// The new window size is smaller than the minimum required size for the current display scale level.

		unsigned int maxDisplayScale = wzGetMaximumDisplayScaleForWindowSize(width, height);
		if (maxDisplayScale < 100)
		{
			// Cannot adjust display scale factor below 1. Desired window size is below the minimum supported.
			debug(LOG_WZ, "Unable to change window size to (%d x %d) because it is smaller than the minimum supported at a 100%% display scale", width, height);
			return false;
		}

		// Adjust the current display scale level to the nearest supported level.
		debug(LOG_WZ, "The current Display Scale (%d%%) is too high for the desired window size. Reducing the current Display Scale to the maximum possible for the desired window size: %d%%.", current_displayScale, maxDisplayScale);
		wzChangeDisplayScale(maxDisplayScale);

		// Store the new display scale
		war_SetDisplayScale(maxDisplayScale);
	}

	// Position the window (centered) on the screen (for its upcoming new size)
	SDL_SetWindowPosition(WZwindow, SDL_WINDOWPOS_CENTERED_DISPLAY(screen), SDL_WINDOWPOS_CENTERED_DISPLAY(screen));

	// Change the window size
	// NOTE: Changing the window size will trigger an SDL window size changed event which will handle recalculating layout.
	SDL_SetWindowSize(WZwindow, width, height);

	// Check that the new size is the desired size
	int resultingWidth, resultingHeight = 0;
	SDL_GetWindowSize(WZwindow, &resultingWidth, &resultingHeight);
	if (resultingWidth != width || resultingHeight != height) {
		// Attempting to set the resolution failed
		debug(LOG_WZ, "Attempting to change the resolution to %dx%d seems to have failed (result: %dx%d).", width, height, resultingWidth, resultingHeight);

		// Revert to the prior position + resolution + display scale, and return false
		SDL_SetWindowSize(WZwindow, prev_width, prev_height);
		SDL_SetWindowPosition(WZwindow, prev_x, prev_y);
		if (current_displayScale != priorDisplayScale)
		{
			// Reverse the correction applied to the Display Scale to support the desired resolution.
			wzChangeDisplayScale(priorDisplayScale);
			war_SetDisplayScale(priorDisplayScale);
		}
		return false;
	}
	return true;
}

// Returns the current window screen, width, and height
void wzGetWindowResolution(unsigned int *width, unsigned int *height)
{
	int currentWidth = 0, currentHeight = 0;
	SDL_GetWindowSize(WZwindow, &currentWidth, &currentHeight);
	assert(currentWidth >= 0);
	assert(currentHeight >= 0);
	if (width != nullptr)
	{
		*width = currentWidth;
	}
	if (height != nullptr)
	{
		*height = currentHeight;
	}
}

void initGL(int width, int height) {
	glViewport(0, 0, width, height);
	glCullFace(GL_FRONT);
	glEnable(GL_CULL_FACE);
}

void pushResolution (int w, int h, int refresh_rate, int i)
{
  struct screeninfo screenlist;
  screenlist.width = w;
  screenlist.height = h;
  screenlist.refresh_rate = refresh_rate;
  screenlist.screen = i;		// which monitor this belongs to
  displaylist.push_back(screenlist);
}

void setWindow (void *ptr)
{
  WZwindow = (SDL_Window *) ptr;
}


// Calculates and returns the scale factor from the SDL window's coordinate system (in points) to the raw
// underlying pixels of the viewport / renderer.
//
// IMPORTANT: This value is *non-inclusive* of any user-configured Game Display Scale.
//
// This exposes what is effectively the SDL window's "High-DPI Scale Factor", if SDL's high-DPI support is enabled and functioning.
//
// In the normal, non-high-DPI-supported case, (in which the context's drawable size in pixels and the window's logical
// size in points are equal) this will return 1.0 for both values.
//
void wzGetWindowToRendererScaleFactor(float *horizScaleFactor, float *vertScaleFactor)
{
	assert(WZwindow != nullptr);

	// Obtain the window context's drawable size in pixels
	int drawableWidth, drawableHeight = 0;
	SDL_GL_GetDrawableSize(WZwindow, &drawableWidth, &drawableHeight);

	// Obtain the logical window size (in points)
	int windowWidth, windowHeight = 0;
	SDL_GetWindowSize(WZwindow, &windowWidth, &windowHeight);

	debug(LOG_WZ, "Window Logical Size (%d, %d) vs Drawable Size in Pixels (%d, %d)", windowWidth, windowHeight, drawableWidth, drawableHeight);

	if (horizScaleFactor != nullptr)
	{
		*horizScaleFactor = ((float)drawableWidth / (float)windowWidth) * current_displayScaleFactor;
	}
	if (vertScaleFactor != nullptr)
	{
		*vertScaleFactor = ((float)drawableHeight / (float)windowHeight) * current_displayScaleFactor;
	}

	int displayIndex = SDL_GetWindowDisplayIndex(WZwindow);
	if (displayIndex < 0)
	{
		debug(LOG_ERROR, "Failed to get the display index for the window because : %s", SDL_GetError());
	}

	float hdpi, vdpi;
	if (SDL_GetDisplayDPI(displayIndex, nullptr, &hdpi, &vdpi) < 0)
	{
		debug(LOG_ERROR, "Failed to get the display DPI because : %s", SDL_GetError());
	}
	else
	{
		debug(LOG_WZ, "Display DPI: %f, %f", hdpi, vdpi);
	}
}

// Calculates and returns the total scale factor from the game's coordinate system (in points)
// to the raw underlying pixels of the viewport / renderer.
//
// IMPORTANT: This value is *inclusive* of both the user-configured "Display Scale" *AND* any underlying
// high-DPI / "Retina" display support provided by SDL.
//
// It is equivalent to: (SDL Window's High-DPI Scale Factor) x (WZ Game Display Scale Factor)
//
// Therefore, if SDL is providing a supported high-DPI window / context, this value will be greater
// than the WZ (user-configured) Display Scale Factor.
//
// It should be used only for internal (non-user-displayed) cases in which the full scaling factor from
// the game system's coordinate system (in points) to the underlying display pixels is required.
// (For example, when rasterizing text for best display.)
//
void wzGetGameToRendererScaleFactor(float *horizScaleFactor, float *vertScaleFactor)
{
	float horizWindowScaleFactor = 0.f, vertWindowScaleFactor = 0.f;
	wzGetWindowToRendererScaleFactor(&horizWindowScaleFactor, &vertWindowScaleFactor);
	assert(horizWindowScaleFactor != 0.f);
	assert(vertWindowScaleFactor != 0.f);

	if (horizScaleFactor != nullptr)
	{
		*horizScaleFactor = horizWindowScaleFactor * current_displayScaleFactor;
	}
	if (vertScaleFactor != nullptr)
	{
		*vertScaleFactor = vertWindowScaleFactor * current_displayScaleFactor;
	}
}

void wzSetWindowIsResizable(bool resizable)
{
	assert(WZwindow != nullptr);
	SDL_bool sdl_resizable = (resizable) ? SDL_TRUE : SDL_FALSE;
	SDL_SetWindowResizable(WZwindow, sdl_resizable);
}

bool wzIsWindowResizable()
{
	Uint32 flags = SDL_GetWindowFlags(WZwindow);
	if (flags & SDL_WINDOW_RESIZABLE)
	{
		return true;
	}
	return false;
}

bool wzSupportsLiveResolutionChanges()
{
	return true;
}

/*!
 * Activation (focus change ... and) eventhandler.  Mainly for debugging.
 */
static void handleActiveEvent(SDL_Event *event)
{
	if (event->type == SDL_WINDOWEVENT)
	{
		switch (event->window.event)
		{
		case SDL_WINDOWEVENT_SHOWN:
			debug(LOG_WZ, "Window %d shown", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_HIDDEN:
			debug(LOG_WZ, "Window %d hidden", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_EXPOSED:
			debug(LOG_WZ, "Window %d exposed", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_MOVED:
			debug(LOG_WZ, "Window %d moved to %d,%d", event->window.windowID, event->window.data1, event->window.data2);
				// FIXME: Handle detecting which screen the window was moved to, and update saved war_SetScreen?
			break;
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
			debug(LOG_WZ, "Window %d resized to %dx%d", event->window.windowID, event->window.data1, event->window.data2);
			{
				unsigned int oldWindowWidth = windowWidth;
				unsigned int oldWindowHeight = windowHeight;

				Uint32 windowFlags = SDL_GetWindowFlags(WZwindow);
				debug(LOG_WZ, "Window resized to window flags: %u", windowFlags);

				int newWindowWidth = 0, newWindowHeight = 0;
				SDL_GetWindowSize(WZwindow, &newWindowWidth, &newWindowHeight);

				if ((event->window.data1 != newWindowWidth) || (event->window.data2 != newWindowHeight))
				{
					// This can happen - so we use the values retrieved from SDL_GetWindowSize in any case - but
					// log it for tracking down the SDL-related causes later.
					debug(LOG_WARNING, "Received width and height (%d x %d) do not match those from GetWindowSize (%d x %d)", event->window.data1, event->window.data2, newWindowWidth, newWindowHeight);
				}

				handleWindowSizeChange(oldWindowWidth, oldWindowHeight, newWindowWidth, newWindowHeight);

				// Store the new values (in case the user manually resized the window bounds)
				war_SetWidth(newWindowWidth);
				war_SetHeight(newWindowHeight);
			}
			break;
		case SDL_WINDOWEVENT_MINIMIZED:
			debug(LOG_WZ, "Window %d minimized", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
			debug(LOG_WZ, "Window %d maximized", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_RESTORED:
			debug(LOG_WZ, "Window %d restored", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_ENTER:
			debug(LOG_WZ, "Mouse entered window %d", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_LEAVE:
			debug(LOG_WZ, "Mouse left window %d", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			mouseInWindow = SDL_TRUE;
			debug(LOG_WZ, "Window %d gained keyboard focus", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			mouseInWindow = SDL_FALSE;
			debug(LOG_WZ, "Window %d lost keyboard focus", event->window.windowID);
			break;
		case SDL_WINDOWEVENT_CLOSE:
			debug(LOG_WZ, "Window %d closed", event->window.windowID);
			break;
		default:
			debug(LOG_WZ, "Window %d got unknown event %d", event->window.windowID, event->window.event);
			break;
		}
	}
}

// Actual mainloop
bool wzMainEventLoop()
{
	SDL_Event event;
  /* Deal with any windows messages */
  while (SDL_PollEvent(&event))
  {
    printf("%i : Event\n", event.type);
    switch (event.type)
    {
    case SDL_KEYUP:
    case SDL_KEYDOWN:
      inputHandleKeyEvent(&event.key);
      break;
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEBUTTONDOWN:
      inputHandleMouseButtonEvent(&event.button);
      break;
    case SDL_MOUSEMOTION:
      inputHandleMouseMotionEvent(event.motion.x, event.motion.y);
      break;
    case SDL_MOUSEWHEEL:
      inputHandleMouseWheelEvent(&event.wheel);
      break;
    case SDL_WINDOWEVENT:
      handleActiveEvent(&event);
      break;
    case SDL_TEXTINPUT:	// SDL now handles text input differently
      inputhandleText(&event.text);
      break;
    case SDL_QUIT:
      return true;
    default:
      // Custom WZ App Event
      if(event.type == wzSDLAppEvent
         && event.user.code == wzSDLAppEventCodes::MAINTHREADEXEC
           && event.user.data1 != nullptr)
      {
        WZ_MAINTHREADEXEC * pExec = static_cast<WZ_MAINTHREADEXEC *>(event.user.data1);
        pExec->doExecOnMainThread();
        delete pExec;
      }
      break;
    }
  }
  // Ideally, we don't want Qt processing events in addition to SDL - this causes
  // all kinds of issues (crashes taking screenshots on Windows, freezing on
  // macOS without a nasty workaround) - but without the following line the script
  // debugger window won't display properly on Linux.
  //
  // Therefore, do not include it on Windows and macOS builds, which does not
  // impact the script debugger's functionality, but include it (for now) on other
  // builds until an alternative script debugger UI is available.
  //
  handleQt();
  //mainLoop();				// WZ does its thing
  //inputNewFrame();			// reset input states
  return false;
}

void handleQt()
{
  appPtr->processEvents();		// Qt needs to do its stuff
  processScreenSizeChangeNotificationIfNeeded();
}

void wzShutdown()
{
	// order is important!
	sdlFreeCursors();
	SDL_DestroyWindow(WZwindow);
	SDL_Quit();
	appPtr->quit();
	delete appPtr;
	appPtr = nullptr;

	// delete copies of argc, argv
	if (copied_argv != nullptr)
	{
		for(int i=0; i < copied_argc; i++) {
			delete [] copied_argv[i];
		}
		delete [] copied_argv;
		copied_argv = nullptr;
		copied_argc = 0;
	}
}
