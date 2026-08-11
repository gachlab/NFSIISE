// SPDX-License-Identifier: MIT

#ifndef WRAPPER_H
#define WRAPPER_H

#if !defined(WIN32) && !defined(_GNU_SOURCE)
	#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef NOT_COMPILING
	#define STDCALL
	#define REGPARM

	#define MAYBE_THIS_SINGLE void
	#define MAYBE_THIS
#elif !defined(NFS_CPP) && (defined(__i386) || defined(__i386__))
	#define STDCALL __attribute__((stdcall))
	#define REGPARM __attribute__((regparm(2))) //First two arguments are compatible with Watcom fastcall

	#define MAYBE_THIS_SINGLE void
	#define MAYBE_THIS
#else
	#define STDCALL
	#define REGPARM

	#define MAYBE_THIS_SINGLE void *this
	#define MAYBE_THIS void *this,

	#ifndef NFS_CPP
		#define NFS_CPP
	#endif
#endif

#if !defined(NFS_CPP) && defined(STACK_REALIGN)
	#define REALIGN __attribute__((force_align_arg_pointer))
#else
	#define REALIGN
#endif

/*
 * A pointer as the game sees it.
 *
 * Any struct shared with the translated game must have the same layout it had
 * in 1997, because the game allocates it (usually on its own stack, at a size
 * it computed then) and reads it back at fixed offsets. A `void *` field is 4
 * bytes in a 32-bit build and 8 in a 64-bit one, which both shifts every
 * following field and makes the struct larger than the space the game
 * reserved -- writing one is then a buffer overflow into the game's stack.
 *
 * So game-facing structs store addresses as GameAddr, never as pointers.
 * Converting is safe in both directions because everything the game can
 * reference lives below 2 GiB; see LowMem.h.
 */
typedef uint32_t GameAddr;
#define GAME_PTR(addr) ((void *)(uintptr_t)(GameAddr)(addr))
#define GAME_ADDR(ptr) ((GameAddr)(uintptr_t)(ptr))

/*
 * Compile-time layout check for those structs. C90 has no _Static_assert, so
 * this is the negative-array-size trick: it fails to compile if the size is
 * wrong, naming the struct in the error.
 */
#define ASSERT_GAME_LAYOUT(type, bytes) \
	typedef char GameLayoutCheck_##type[(sizeof(type) == (bytes)) ? 1 : -1]

#define MAX_PATH 260

#define BOOL int32_t
#define false 0
#define true 1

typedef uint32_t (STDCALL *WindowProc)(MAYBE_THIS void *hWnd, uint32_t uMsg, uint32_t wParam, uint32_t lParam);

char *convertFilePath(const char *srcPth, BOOL convToLower);

#endif // WRAPPER_H
