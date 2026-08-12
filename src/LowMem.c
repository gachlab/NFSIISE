// SPDX-License-Identifier: MIT

#include "LowMem.h"

#include <stdint.h>
#include <string.h>

void nfsBadAddr(unsigned long long addr)
{
	fprintf(stderr, "NFS: game address %#llx does not fit in 32 bits.\n"
	                "     A host pointer has reached a slot the game truncates.\n", addr);
	abort();
}

/*
 * The original build runs the real 1997 binary through the assembly
 * translation, where an address is an address and there is no arena at all.
 * Only the C++ translation has one, so that -- not the host's word size -- is
 * what decides whether any of this is needed.
 */
#ifndef NFS_CPP

void lowMemInit(void) {}
void *lowMemAlloc(size_t size) { return malloc(size); }
void *lowMemCalloc(size_t num, size_t size) { return calloc(num, size); }
void *lowMemRealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void lowMemFree(void *ptr) { free(ptr); }
BOOL lowMemIsAddressable(const void *ptr) { (void)ptr; return true; }

#else

#include <pthread.h>

/*
 * The heap lives inside the game's arena, which the translated game defines
 * (see tools/patch_cpp_64bit). It has to: every address the game holds is an
 * offset from the arena's base, so a block handed out from anywhere else has
 * no offset to be named by.
 *
 * This replaces a 256 MiB mmap that had to land below 2 GiB, which is the
 * requirement that could never be met on arm64 macOS or Android. Ordinary
 * static storage has no such problem, and being uninitialised it is still
 * demand-zero -- address space rather than memory.
 */
extern unsigned char *const g_nfsHeap;
extern const unsigned long g_nfsHeapBytes;

#define LOWMEM_ALIGN       16u

/*
 * Blocks are kept in a single physically-ordered list with boundary tags, and
 * allocation is first fit. That is not a sophisticated allocator, but the game
 * allocates rarely and in modest numbers, and being obviously correct matters
 * far more here than being fast: a bug in this file corrupts the game's memory
 * in ways that look like a rendering bug three subsystems away.
 */
typedef struct LowBlock
{
	size_t size;            /* usable bytes following this header */
	struct LowBlock *next;  /* next block in address order, NULL if last */
	struct LowBlock *prev;  /* previous block in address order */
	uint32_t free;
	uint32_t guard;
} LowBlock;

#define LOWMEM_GUARD 0x10E3A1EDu

static uint8_t *g_arena;
static LowBlock *g_first;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t alignUp(size_t value)
{
	return (value + (LOWMEM_ALIGN - 1)) & ~(size_t)(LOWMEM_ALIGN - 1);
}

/* Non-zero if the pointer is one the game can name, i.e. inside the arena. */
BOOL lowMemIsAddressable(const void *ptr)
{
	return ptr == NULL ||
		((const unsigned char *)ptr >= g_nfsHeap &&
		 (const unsigned char *)ptr < g_nfsHeap + g_nfsHeapBytes);
}

void lowMemInit(void)
{
	if (g_arena)
		return;

	/*
	 * Before anything else: the game's statics have to be in place, because
	 * the wrapper reads them through exported symbols of its own accord.
	 */
	nfsArenaInit();

	g_arena = g_nfsHeap;

	g_first = (LowBlock *)g_arena;
	g_first->size = (size_t)g_nfsHeapBytes - sizeof(LowBlock);
	g_first->next = NULL;
	g_first->prev = NULL;
	g_first->free = 1;
	g_first->guard = LOWMEM_GUARD;
}

/* Splits a block when the leftover is worth tracking. Caller holds the lock. */
static void splitBlock(LowBlock *block, size_t wanted)
{
	LowBlock *rest;

	if (block->size < wanted + sizeof(LowBlock) + LOWMEM_ALIGN)
		return;

	rest = (LowBlock *)((uint8_t *)block + sizeof(LowBlock) + wanted);
	rest->size = block->size - wanted - sizeof(LowBlock);
	rest->free = 1;
	rest->guard = LOWMEM_GUARD;
	rest->prev = block;
	rest->next = block->next;
	if (rest->next)
		rest->next->prev = rest;

	block->size = wanted;
	block->next = rest;
}

void *lowMemAlloc(size_t size)
{
	LowBlock *block;
	void *result = NULL;

	if (size == 0)
		size = 1;
	size = alignUp(size);

	pthread_mutex_lock(&g_lock);

	if (!g_arena)
	{
		pthread_mutex_unlock(&g_lock);
		lowMemInit();
		pthread_mutex_lock(&g_lock);
	}

	for (block = g_first; block; block = block->next)
	{
		if (block->free && block->size >= size)
		{
			splitBlock(block, size);
			block->free = 0;
			result = (uint8_t *)block + sizeof(LowBlock);
			break;
		}
	}

	pthread_mutex_unlock(&g_lock);

	if (!result)
		fprintf(stderr, "LowMem: out of low memory allocating %zu bytes\n", size);

	return result;
}

void *lowMemCalloc(size_t num, size_t size)
{
	size_t total = num * size;
	void *result;

	/* The game is 32-bit; a request this large is a bug, not a request. */
	if (num != 0 && total / num != size)
		return NULL;

	result = lowMemAlloc(total);
	if (result)
		memset(result, 0, total);
	return result;
}

void lowMemFree(void *ptr)
{
	LowBlock *block;

	if (!ptr)
		return;

	block = (LowBlock *)((uint8_t *)ptr - sizeof(LowBlock));

	pthread_mutex_lock(&g_lock);

	if (block->guard != LOWMEM_GUARD)
	{
		pthread_mutex_unlock(&g_lock);
		fprintf(stderr, "LowMem: free of %p, which this allocator did not hand out\n", ptr);
		return;
	}

	block->free = 1;

	/* Coalesce forwards then backwards, so runs of frees do not fragment. */
	while (block->next && block->next->free)
	{
		LowBlock *dead = block->next;
		block->size += sizeof(LowBlock) + dead->size;
		block->next = dead->next;
		if (block->next)
			block->next->prev = block;
		dead->guard = 0;
	}
	while (block->prev && block->prev->free)
	{
		LowBlock *prev = block->prev;
		prev->size += sizeof(LowBlock) + block->size;
		prev->next = block->next;
		if (prev->next)
			prev->next->prev = prev;
		block->guard = 0;
		block = prev;
	}

	pthread_mutex_unlock(&g_lock);
}

void *lowMemRealloc(void *ptr, size_t size)
{
	LowBlock *block;
	void *result;
	size_t copy;

	if (!ptr)
		return lowMemAlloc(size);
	if (size == 0)
	{
		lowMemFree(ptr);
		return NULL;
	}

	block = (LowBlock *)((uint8_t *)ptr - sizeof(LowBlock));
	if (block->guard != LOWMEM_GUARD)
	{
		fprintf(stderr, "LowMem: realloc of %p, which this allocator did not hand out\n", ptr);
		return NULL;
	}

	if (block->size >= alignUp(size))
		return ptr;

	result = lowMemAlloc(size);
	if (!result)
		return NULL;

	copy = block->size < size ? block->size : size;
	memcpy(result, ptr, copy);
	lowMemFree(ptr);
	return result;
}

#endif
