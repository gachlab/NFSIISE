// SPDX-License-Identifier: MIT

#include "GameVarargs.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#if !defined(__LP64__) && !defined(_WIN64)

/* 32-bit host: the game's argument block already is a va_list. */
int32_t gameVsprintf(char *out, const char *fmt, const void *args)
{
	return vsprintf(out, fmt, *(va_list *)&args);
}

#else

/*
 * The game's arguments are consecutive 4-byte slots, in Win32 cdecl order.
 * Integers, chars and pointers take one slot; doubles take two. Nothing here
 * ever sees an 8-byte integer, because the game has no 64-bit types.
 */
typedef struct
{
	const uint8_t *cursor;
} ArgReader;

static uint32_t nextWord(ArgReader *reader)
{
	uint32_t value;
	memcpy(&value, reader->cursor, sizeof value);
	reader->cursor += 4;
	return value;
}

static double nextDouble(ArgReader *reader)
{
	double value;
	memcpy(&value, reader->cursor, sizeof value);
	reader->cursor += 8;
	return value;
}

/*
 * Copies one conversion specification out of the format string, dropping any
 * length modifier. The game's ints are 32 bits regardless of what it wrote, so
 * "%ld" must become "%d" here -- left as-is it would make libc read 8 bytes
 * from an int argument.
 */
static const char *copySpec(const char *fmt, char *spec, size_t specSize, char *conversion)
{
	size_t n = 0;

	*conversion = 0;

	if (n < specSize - 1)
		spec[n++] = '%';

	/* flags */
	while (*fmt && strchr("-+ #0'", *fmt))
	{
		if (n < specSize - 1)
			spec[n++] = *fmt;
		++fmt;
	}
	/* width */
	while ((*fmt >= '0' && *fmt <= '9') || *fmt == '*')
	{
		if (n < specSize - 1)
			spec[n++] = *fmt;
		++fmt;
	}
	/* precision */
	if (*fmt == '.')
	{
		if (n < specSize - 1)
			spec[n++] = *fmt;
		++fmt;
		while ((*fmt >= '0' && *fmt <= '9') || *fmt == '*')
		{
			if (n < specSize - 1)
				spec[n++] = *fmt;
			++fmt;
		}
	}
	/* length modifier: consumed and discarded, see above */
	while (*fmt && strchr("hlLqjzt", *fmt))
		++fmt;

	if (!*fmt)
	{
		spec[n] = 0;
		return fmt;
	}

	*conversion = *fmt;
	if (n < specSize - 1)
		spec[n++] = *fmt;
	++fmt;
	spec[n] = 0;

	return fmt;
}

/* Counts the '*' in a spec, each of which eats an int argument before the value. */
static int starCount(const char *spec)
{
	int count = 0;
	for (; *spec; ++spec)
	{
		if (*spec == '*')
			++count;
	}
	return count;
}

int32_t gameVsprintf(char *out, const char *fmt, const void *args)
{
	ArgReader reader;
	char *cursor = out;

	reader.cursor = (const uint8_t *)args;

	while (*fmt)
	{
		char spec[64];
		char conversion;
		int width = 0, precision = 0;
		int stars, written = 0;

		if (*fmt != '%')
		{
			*cursor++ = *fmt++;
			continue;
		}
		++fmt;

		if (*fmt == '%')
		{
			*cursor++ = '%';
			++fmt;
			continue;
		}

		fmt = copySpec(fmt, spec, sizeof spec, &conversion);
		if (!conversion)
			break;

		stars = starCount(spec);
		if (stars >= 1)
			width = (int32_t)nextWord(&reader);
		if (stars >= 2)
			precision = (int32_t)nextWord(&reader);

		switch (conversion)
		{
			case 'd':
			case 'i':
			{
				int32_t value = (int32_t)nextWord(&reader);
				if (stars == 2)
					written = sprintf(cursor, spec, width, precision, value);
				else if (stars == 1)
					written = sprintf(cursor, spec, width, value);
				else
					written = sprintf(cursor, spec, value);
				break;
			}
			case 'u':
			case 'o':
			case 'x':
			case 'X':
			{
				uint32_t value = nextWord(&reader);
				if (stars == 2)
					written = sprintf(cursor, spec, width, precision, value);
				else if (stars == 1)
					written = sprintf(cursor, spec, width, value);
				else
					written = sprintf(cursor, spec, value);
				break;
			}
			case 'c':
			{
				int value = (int)nextWord(&reader);
				if (stars == 1)
					written = sprintf(cursor, spec, width, value);
				else
					written = sprintf(cursor, spec, value);
				break;
			}
			case 's':
			case 'p':
			{
				/*
				 * A 32-bit game address. Widening is safe precisely because
				 * everything the game can reference lives below 2 GiB -- see
				 * LowMem.h. A null is printed the way libc would.
				 */
				uint32_t address = nextWord(&reader);
				const void *pointer = (const void *)(uintptr_t)address;
				if (conversion == 's' && address == 0)
					pointer = "(null)";
				if (stars == 2)
					written = sprintf(cursor, spec, width, precision, pointer);
				else if (stars == 1)
					written = sprintf(cursor, spec, width, pointer);
				else
					written = sprintf(cursor, spec, pointer);
				break;
			}
			case 'f':
			case 'F':
			case 'e':
			case 'E':
			case 'g':
			case 'G':
			case 'a':
			case 'A':
			{
				double value = nextDouble(&reader);
				if (stars == 2)
					written = sprintf(cursor, spec, width, precision, value);
				else if (stars == 1)
					written = sprintf(cursor, spec, width, value);
				else
					written = sprintf(cursor, spec, value);
				break;
			}
			case 'n':
				/* Deliberately unsupported: it writes through a caller pointer
				 * and the game has no legitimate use for it. */
				nextWord(&reader);
				break;
			default:
				/* Unknown conversion: emit it literally rather than guessing at
				 * an argument width and desynchronising everything after it. */
				written = sprintf(cursor, "%s", spec);
				break;
		}

		if (written > 0)
			cursor += written;
	}

	*cursor = 0;
	return (int32_t)(cursor - out);
}

#endif
