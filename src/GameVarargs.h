// SPDX-License-Identifier: MIT

#ifndef GAMEVARARGS_H
#define GAMEVARARGS_H

#include "Wrapper.h"

/*
 * printf-family bridge between the game's calling convention and the host's.
 *
 * The game calls vsprintf with a pointer to its own stack, because under Win32
 * cdecl a va_list *is* just a pointer to consecutive 4-byte argument slots.
 * That happens to be exactly what a 32-bit host va_list is too, so on a 32-bit
 * build this can be handed straight to libc.
 *
 * On x86-64 it cannot. A System V va_list is not a pointer at all -- it is a
 * struct describing a register save area, an overflow area and how much of each
 * has been consumed. Passing the game's stack pointer as one reads garbage and
 * crashes. Every argument is also 4 bytes wide where the host expects 8 for
 * pointers and longs.
 *
 * So on 64-bit the format string is parsed here, each argument is pulled out of
 * the game's 4-byte slots by hand, and the actual formatting of each individual
 * conversion is delegated back to libc snprintf. That keeps exact printf
 * semantics -- flags, widths, precisions, float rounding -- without
 * reimplementing any of it.
 */

int32_t gameVsprintf(char *out, const char *fmt, const void *args);

#endif // GAMEVARARGS_H
