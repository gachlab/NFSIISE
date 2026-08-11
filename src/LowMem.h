// SPDX-License-Identifier: MIT

#ifndef LOWMEM_H
#define LOWMEM_H

#include "Wrapper.h"

#include <stddef.h>

/*
 * Allocator that only ever returns addresses below 2 GiB.
 *
 * The translated game (src/Cpp/NFS2SE.cpp) stores real addresses in int32_t
 * slots -- registers, its simulated stack, and every pushed pointer. On a
 * 32-bit build that was free. On a 64-bit build it still works, but only if
 * every address the game can observe fits in 32 bits, and stays positive so
 * the truncated value does not sign-extend back into garbage.
 *
 * Three things have to satisfy that:
 *
 *   * Static data and code. Free with -no-pie: .data and .text land around
 *     0x400000. This is why a 64-bit build MUST NOT be position independent.
 *   * The game's heap, which reaches libc through malloc_wrap/calloc_wrap/
 *     free_wrap in Wrapper.c.
 *   * Wrapper-owned objects handed back to the game as opaque handles
 *     (threads, events, file mappings, find handles in Kernel32.c).
 *
 * The last two route here instead of to libc malloc, which on x86-64 Linux
 * hands out addresses around 0x7f..., far outside the range that survives
 * truncation.
 *
 * On a 32-bit build these are plain wrappers around libc and cost nothing.
 */

void  lowMemInit(void);
void *lowMemAlloc(size_t size);
void *lowMemCalloc(size_t num, size_t size);
void *lowMemRealloc(void *ptr, size_t size);
void  lowMemFree(void *ptr);

/* Non-zero if the pointer survives a round trip through int32_t. */
BOOL lowMemIsAddressable(const void *ptr);

#endif // LOWMEM_H
