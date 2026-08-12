/*
 * One kernel, five address models. Each is compiled into its own translation
 * unit so the compiler sees what it sees in NFS2SE.cpp: a base defined
 * elsewhere, opaque, and a long straight run of accesses through 32-bit
 * addresses.
 *
 * MODEL_DIRECT   *(T *)addr -- what the game does today, and what neither
 *                macOS nor Android arm64 can do, both reserving the low 4 GiB
 * MODEL_GLOBAL   base + offset, base in a mutable global
 * MODEL_CONSTG   base + offset, base in a const global
 * MODEL_NOTRUNC  as CONSTG, but sign-extending the offset instead of
 *                truncating it -- the same value while offsets stay positive
 * MODEL_FIXED    base + offset, base a compile-time constant
 */
#include <stdint.h>
#include <stddef.h>

/* Somewhere the low 4 GiB are not reserved and a fixed mapping can land. */
#define FIXED_BASE 0x400000000ull

#if defined(MODEL_DIRECT)
#  define NFS_MEM(addr) ((void *)(addr))
#elif defined(MODEL_GLOBAL)
extern uintptr_t g_base;
#  define NFS_MEM(addr) ((void *)(g_base + (uint32_t)(uintptr_t)(addr)))
#elif defined(MODEL_CONSTG)
extern uintptr_t const g_base_const;
#  define NFS_MEM(addr) ((void *)(g_base_const + (uint32_t)(uintptr_t)(addr)))
#elif defined(MODEL_NOTRUNC)
extern uintptr_t const g_base_const;
#  define NFS_MEM(addr) ((void *)(g_base_const + (uintptr_t)(addr)))
#elif defined(MODEL_FIXED)
#  define NFS_MEM(addr) ((void *)(FIXED_BASE + (uint32_t)(uintptr_t)(addr)))
#else
#  error pick a model
#endif

/* The helpers, as they read in the translated game. */
template<typename T> static inline int32_t &to32i(const T addr) { return *(int32_t *)NFS_MEM(addr); }
template<typename T> static inline int16_t &to16i(const T addr) { return *(int16_t *)NFS_MEM(addr); }
template<typename T> static inline int8_t  &to8i (const T addr) { return *(int8_t  *)NFS_MEM(addr); }
template<typename T> static inline float   &to32f(const T addr) { return *(float   *)NFS_MEM(addr); }

/*
 * Shaped like the translated code: a frame pointer in a register plus a
 * constant displacement (the simulated stack), an absolute address (a _data
 * macro), and a scaled index (a pointer table walked at a stride of four). The
 * serial dependencies are deliberate; the translated code is a chain of them.
 */
extern "C" int32_t kernel(int32_t esp, int32_t data, int32_t table, int n)
{
	int32_t eax = 1, ebx = 2, ecx = 3, edx = 0;
	for (int i = 0; i < n; ++i) {
		/* stack traffic: base register plus constant displacement */
		to32i(esp + 0x40) = eax;
		to32i(esp + 0x44) = ebx;
		to16i(esp + 0x3C) = (int16_t)ecx;
		eax = to32i(esp + 0x40);
		ebx = to32i(esp + 0x44);

		/* absolute static data: a _data macro */
		to32i(data + 0x10) = eax + ebx;
		ecx = to32i(data + 0x10);
		edx = to8i(data + 0x21);

		/* scaled index into a pointer table */
		edx = to32i(table + (ecx & 0xFF) * 4);
		eax += edx;

		/* float traffic, of which the game does plenty */
		to32f(data + 0x30) = (float)eax * 0.5f;
		ebx += (int32_t)to32f(data + 0x30);

		/* dependent load-to-address: the worst case for the extra add */
		ecx = to32i(table + (edx & 0x3F) * 4);
		eax ^= ecx;
	}
	return eax + ebx + ecx + edx;
}
