// SPDX-License-Identifier: MIT

#ifndef LOWMEM_H
#define LOWMEM_H

#include "Wrapper.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Allocator that only ever returns memory the game can name.
 *
 * The translated game (src/Cpp/NFS2SE.cpp) does not store addresses at all:
 * it stores offsets from the base of one arena, so anything it can reach has
 * to live inside that arena. Ordinary malloc does not, which is why the game's
 * heap (malloc_wrap and friends in Wrapper.c) and every wrapper-owned object
 * handed back as an opaque handle (threads, events, file mappings, find
 * handles in Kernel32.c) come from here instead.
 *
 * This used to be a mapping that had to land below 2 GiB. It no longer is, and
 * that is the entire reason the offset model exists: arm64 macOS and Android
 * reserve the low 4 GiB outright, so no such mapping can ever be made there.
 *
 * The assembly build, which runs the real 1997 binary, has no arena and no
 * need of one; there these are plain wrappers around libc.
 */

void  lowMemInit(void);
void *lowMemAlloc(size_t size);
void *lowMemCalloc(size_t num, size_t size);
void *lowMemRealloc(void *ptr, size_t size);
void  lowMemFree(void *ptr);

/* Non-zero if the pointer is one the game can name, i.e. inside the arena. */
BOOL lowMemIsAddressable(const void *ptr);

/*
 * Reports a game address that does not fit in 32 bits, and aborts.
 *
 * That can only mean a real host pointer reached a slot that holds an offset
 * -- a wrapper handing over the address of one of its own locals, say. It is
 * the single failure mode this address model has, and the one that otherwise
 * surfaces as corruption in an unrelated subsystem hours later. Compile the
 * translated game with -DNFS_CHECK_ADDR to have every access check.
 */
void nfsBadAddr(unsigned long long addr);

/*
 * Puts the game's static data where the game will look for it.
 *
 * Defined by the translated game (see tools/patch_cpp_64bit): _data now lives
 * in an arena that is demand-zero, so its initial contents have to be copied
 * in. lowMemInit calls it, which is the first thing main does, so that it
 * cannot be beaten to the punch: the wrapper reads the game's statics through
 * ten exported symbols -- binaryGameVersion, mousePositionX and the rest --
 * and those reads are scattered rather than confined to one entry point.
 */
void nfsArenaInit(void);

#endif // LOWMEM_H
