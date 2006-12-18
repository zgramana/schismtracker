#ifndef __sdlmain_header
#define __sdlmain_header

/* Hack to get the build to shut up about SDL trampling on names. */
#define index qwwqwqqw
#define access ddsdsd

/* just a fancy way to get SDL headers */

#ifndef _USE_AUTOCONF

#ifdef _SDL_IN_PATH
#undef _SDL_BY_PATH
#else
#define _SDL_BY_PATH

#ifdef _MSC_VER
#pragma comment(lib, "SDL.lib")
#pragma comment(lib, "SDLmain.lib")
#endif
#endif

#endif

#ifdef _SDL_BY_PATH
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#ifndef __cplusplus
#include <SDL/SDL_syswm.h>
#include <SDL/SDL_opengl.h>
#endif
#else
#include <SDL.h>
#include <SDL_thread.h>

#ifndef __cplusplus
#include <SDL_syswm.h>
#include <SDL_opengl.h>
#endif
#endif

/* complement to above hack */
#undef index
#undef access

#endif
