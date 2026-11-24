#pragma once

// Include this instead of <windows.h> anywhere in the Windows Host.
//
// It pins the API surface (Windows 10), keeps out of the min/max and media
// headers, and undefines SDK object-like macros that collide with portable
// core identifiers (enum member names in core/protocol_types.hpp such as
// UP/DOWN/ERROR must stay usable unqualified in Win32 translation units).
//
// Include the core/network headers FIRST in a translation unit when in
// doubt: their include guards then also protect them from later SDK
// declarations.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>

// SDK object-like macros that would break portable identifiers.
// Guarded: undefining a name the SDK did not define is harmless.
#ifdef ERROR
#undef ERROR
#endif
#ifdef OK
#undef OK
#endif
#ifdef DOWN
#undef DOWN
#endif
#ifdef UP
#undef UP
#endif
#ifdef LEFT
#undef LEFT
#endif
#ifdef RIGHT
#undef RIGHT
#endif
#ifdef MIDDLE
#undef MIDDLE
#endif
#ifdef DOUBLE_CLICK
#undef DOUBLE_CLICK
#endif
#ifdef MOVE
#undef MOVE
#endif
#ifdef WHEEL
#undef WHEEL
#endif
#ifdef TEXT
#undef TEXT
#endif
#ifdef DATA
#undef DATA
#endif
#ifdef KEY
#undef KEY
#endif
#ifdef STATE
#undef STATE
#endif
#ifdef MINIMIZE
#undef MINIMIZE
#endif
#ifdef MAXIMIZE
#undef MAXIMIZE
#endif
#ifdef RESTORE
#undef RESTORE
#endif
#ifdef STOPPED
#undef STOPPED
#endif
#ifdef HIDDEN
#undef HIDDEN
#endif
#ifdef CLOSED
#undef CLOSED
#endif
#ifdef CLOSE
#undef CLOSE
#endif
#ifdef APP
#undef APP
#endif
#ifdef MENU
#undef MENU
#endif
#ifdef SIZE
#undef SIZE
#endif
#ifdef INPUT
#undef INPUT
#endif
#ifdef OUTPUT
#undef OUTPUT
#endif
#ifdef READ
#undef READ
#endif
#ifdef WRITE
#undef WRITE
#endif
#ifdef LOCK
#undef LOCK
#endif
#ifdef UNLOCK
#undef UNLOCK
#endif
#ifdef PAUSE
#undef PAUSE
#endif
#ifdef RESUME
#undef RESUME
#endif
#ifdef ACCEPTED
#undef ACCEPTED
#endif
#ifdef DENIED
#undef DENIED
#endif
#ifdef SLEEP
#undef SLEEP
#endif
#ifdef SHUTDOWN
#undef SHUTDOWN
#endif
#ifdef IDLE
#undef IDLE
#endif
#ifdef RUNNING
#undef RUNNING
#endif
#ifdef DEGRADED
#undef DEGRADED
#endif
