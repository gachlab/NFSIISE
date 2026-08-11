// SPDX-License-Identifier: MIT

#include "LowMem.h"

#include <stdint.h>
#include <string.h>

/*
 * On a 32-bit build every address already fits in an int32_t, so there is
 * nothing to do and this is a straight pass-through to libc.
 */
#if !defined(__LP64__) && !defined(_WIN64)

void lowMemInit(void) {}
void *lowMemAlloc(size_t size) { return malloc(size); }
void *lowMemCalloc(size_t num, size_t size) { return calloc(num, size); }
void *lowMemRealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void lowMemFree(void *ptr) { free(ptr); }
BOOL lowMemIsAddressable(const void *ptr) { (void)ptr; return true; }

#else

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Address space reserved below 2 GiB. Only touched pages are ever committed
 * (MAP_NORESERVE), so this costs address space rather than memory. NFS2 SE is
 * a 1997 game that shipped targeting 16 MiB machines, so this is very roomy.
 */
#define LOWMEM_ARENA_BYTES (256u * 1024u * 1024u)
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

BOOL lowMemIsAddressable(const void *ptr)
{
	return ptr == NULL || (uintptr_t)ptr < 0x80000000u;
}

void lowMemInit(void)
{
	void *mapped;

	if (g_arena)
		return;

	mapped = MAP_FAILED;

#ifdef MAP_32BIT
	/* Linux: exactly what is wanted -- guarantees the first 2 GiB. */
	mapped = mmap(NULL, LOWMEM_ARENA_BYTES, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_32BIT, -1, 0);
#endif

	if (mapped == MAP_FAILED)
	{
		/*
		 * No MAP_32BIT (macOS, the BSDs). Ask for a low address by hint and
		 * walk upwards until something takes. On macOS this additionally
		 * requires the executable to be linked with -pagezero_size 0x1000,
		 * otherwise the first 4 GiB are reserved by __PAGEZERO and every one
		 * of these attempts fails.
		 */
		uintptr_t hint;
		for (hint = 0x10000000u; hint < 0x70000000u; hint += 0x10000000u)
		{
			mapped = mmap((void *)hint, LOWMEM_ARENA_BYTES, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
			if (mapped != MAP_FAILED)
			{
				if ((uintptr_t)mapped + LOWMEM_ARENA_BYTES < 0x80000000u)
					break;
				munmap(mapped, LOWMEM_ARENA_BYTES);
				mapped = MAP_FAILED;
			}
		}
	}

	if (mapped == MAP_FAILED)
	{
		fprintf(stderr,
			"LowMem: cannot reserve %u MiB below 2 GiB.\n"
			"The translated game stores addresses in 32-bit slots, so it cannot run without this.\n",
			LOWMEM_ARENA_BYTES / (1024u * 1024u));
		abort();
	}

	if (!lowMemIsAddressable((uint8_t *)mapped + LOWMEM_ARENA_BYTES - 1))
	{
		fprintf(stderr, "LowMem: arena at %p does not fit below 2 GiB.\n", mapped);
		abort();
	}

	g_arena = (uint8_t *)mapped;

	g_first = (LowBlock *)g_arena;
	g_first->size = LOWMEM_ARENA_BYTES - sizeof(LowBlock);
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
