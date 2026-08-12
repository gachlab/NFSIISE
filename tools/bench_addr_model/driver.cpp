/* Driver: sets up the arena for one address model and times the kernel. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>

#define FIXED_BASE 0x400000000ull
#define ARENA_SIZE (16u << 20)

extern "C" int32_t kernel(int32_t esp, int32_t data, int32_t table, int n);

static void *arena_map(void)
{
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	void *hint = NULL;
#if defined(MODEL_FIXED)
	hint = (void *)FIXED_BASE;
	flags |= MAP_FIXED;
#elif defined(MODEL_DIRECT)
#  ifdef MAP_32BIT
	flags |= MAP_32BIT;   /* today's model needs a real address under 2 GiB */
#  else
	hint = (void *)0x10000000ull;
#  endif
#endif
	void *p = mmap(hint, ARENA_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
#if defined(MODEL_DIRECT)
	if ((uintptr_t)p + ARENA_SIZE >= 0x80000000ull) {
		fprintf(stderr, "arena at %p is not addressable in 32 bits\n", p);
		exit(1);
	}
#elif defined(MODEL_FIXED)
	if ((uintptr_t)p != FIXED_BASE) {
		fprintf(stderr, "arena landed at %p, not the fixed base\n", p);
		exit(1);
	}
#endif
	memset(p, 0, ARENA_SIZE);
	return p;
}

#if defined(MODEL_GLOBAL)
uintptr_t g_base;
static void *arena;
static void arena_init(void) { arena = arena_map(); g_base = (uintptr_t)arena; }
#elif defined(MODEL_CONSTG) || defined(MODEL_NOTRUNC)
/*
 * A const object at namespace scope with a dynamic initialiser: other TUs may
 * legally assume it never changes, so the load can be hoisted out of loops.
 */
extern uintptr_t const g_base_const;
uintptr_t const g_base_const = (uintptr_t)arena_map();
static void arena_init(void) {}
#  define arena ((void *)g_base_const)
#else
static void *arena;
static void arena_init(void) { arena = arena_map(); }
#endif

/* Where the kernel's three "registers" point, as the model wants them. */
static int32_t addr_of(size_t off)
{
#if defined(MODEL_DIRECT)
	return (int32_t)(uintptr_t)((char *)arena + off);
#else
	(void)arena;
	return (int32_t)off;
#endif
}

static double now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	const int n = (argc > 1) ? atoi(argv[1]) : 20000000;
	const int reps = (argc > 2) ? atoi(argv[2]) : 9;

	arena_init();
	/* Hot, L1/L2-resident, like the simulated stack and the hot statics. */
	const int32_t esp   = addr_of(0x1000);
	const int32_t data  = addr_of(0x2000);
	const int32_t table = addr_of(0x3000);

	int32_t sink = 0;
	double best = 1e30, total = 0;
	for (int r = 0; r < reps; ++r) {
		double t0 = now_s();
		sink += kernel(esp, data, table, n);
		double dt = now_s() - t0;
		total += dt;
		if (dt < best) best = dt;
	}
	/* 12 accesses per iteration -- keep in sync with kernel.h. */
	const double accesses = (double)n * 12.0;
	printf("%-8s best %.4f s  mean %.4f s  %.3f ns/iter  %.4f ns/access  (sink %d)\n",
	       MODEL_NAME, best, total / reps, best / n * 1e9, best / accesses * 1e9, sink);
	return 0;
}
