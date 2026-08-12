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
 *
 * A GameAddr is not an address: it is a distance from the base of the arena
 * everything the game can reach lives in. That indirection is what lets the
 * game run where the low 4 GiB cannot be mapped -- macOS and Android on arm64
 * both reserve them -- and it is why converting either way needs the base.
 * See LowMem.h and tools/patch_cpp_64bit.
 */
typedef uint32_t GameAddr;

/*
 * The base of that arena. Defined by the translated game, next to the arena
 * itself, and declared here rather than in LowMem.h because the conversions
 * below are what need it and LowMem.h includes this file, not the reverse.
 */
#ifdef NFS_CPP
extern const uintptr_t g_nfsBase;
#else
/*
 * The assembly build runs the real 1997 binary, which lives at its own real
 * addresses. There is no arena and no base, so the conversions below are the
 * identity they always were.
 */
#define g_nfsBase ((uintptr_t)0)
#endif

/*
 * Functions rather than macros, for two reasons that both used to be free.
 *
 * Zero has to keep meaning "no address": it did automatically while a GameAddr
 * was a truncated pointer, and adding a base to it would now yield the first
 * byte of the arena, which is a perfectly good pointer and a completely wrong
 * answer. Nothing is placed there, and these map it to and from NULL.
 *
 * And the argument must be evaluated once. Several call sites read
 * GAME_ADDR(lowMemCalloc(num, size)); a macro with a conditional in it would
 * allocate twice and hand back the block it then leaked.
 */
static inline void *gamePtr(GameAddr addr)
{
	return addr ? (void *)(g_nfsBase + (uintptr_t)addr) : NULL;
}
#if defined(NFS_CPP) && defined(NFS_CHECK_ADDR)
extern const unsigned long g_nfsArenaBytes;
void nfsBadAddr(unsigned long long addr);
#endif

static inline GameAddr gameAddr(const void *ptr)
{
#if defined(NFS_CPP) && defined(NFS_CHECK_ADDR)
	/*
	 * Catch it here rather than where the game trips over it.
	 *
	 * Only memory inside the arena has an offset to be named by. Handing over
	 * anything else -- a wrapper static, one of its locals, a libc allocation
	 * -- produces a number that is not an address of anything, and the game
	 * will not fail until it dereferences it, somewhere else entirely.
	 */
	if (ptr && ((uintptr_t)ptr < g_nfsBase ||
	            (uintptr_t)ptr >= g_nfsBase + g_nfsArenaBytes))
		nfsBadAddr((unsigned long long)(uintptr_t)ptr);
#endif
	return ptr ? (GameAddr)((uintptr_t)ptr - g_nfsBase) : 0;
}
#define GAME_PTR(addr) gamePtr(addr)
#define GAME_ADDR(ptr) gameAddr(ptr)

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

/*
 * A libc FILE as the game holds it: a low-memory cell containing the real
 * pointer, referred to by GameAddr. See fopen_wrap / fclose_wrap.
 */
typedef struct
{
	FILE *file;
} GameFile;

/*
 * Calls a game function the wrapper is holding. Under the dispatch-table model
 * those are indices, not addresses, so they cannot simply be called -- see
 * tools/patch_cpp_64bit. Defined by the translated game.
 */
#ifdef NFS_CPP
void nfsCallGameFunction(uint32_t id, void *gameContext);
#endif

FILE *gameFileResolve(GameAddr handle);
GameAddr gameFileWrap(FILE *file);

#endif // WRAPPER_H
