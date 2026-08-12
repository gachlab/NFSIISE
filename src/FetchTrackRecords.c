// SPDX-License-Identifier: MIT

#include "Wrapper.h"
#include "LowMem.h"

#include <string.h>

REGPARM void sub_41B250(MAYBE_THIS uint32_t arg1, GameAddr arg2);

#ifdef NFS_CPP
	#define sub_41B250(a, b) \
		sub_41B250(this, a, b)
#endif

typedef struct
{
	char name[10];
	int16_t car;
	int32_t time;
	int32_t mode;
} StfEntry;
typedef struct
{
	StfEntry bestLap;
	StfEntry laps[3][10];
} Stf;

/*
 * The game holds the result in a 32-bit register, so it cannot be a libc FILE
 * pointer -- those live far above 2 GiB on a 64-bit host. gameFileWrap parks
 * the real pointer in low memory and returns a handle the game can keep.
 *
 * Note this file's own callers want the FILE directly, so they go through
 * openFile() rather than round-tripping through a handle.
 */
static FILE *openFile(const char *fileName, const char *mode)
{
	char *tmpFileName = convertFilePath(fileName, true);
	FILE *f = fopen(tmpFileName, mode);
	free(tmpFileName);
	return f;
}
REALIGN REGPARM GameAddr fopen_wrap(GameAddr fileNameAddr, GameAddr modeAddr)
{
	return gameFileWrap(openFile((const char *)GAME_PTR(fileNameAddr),
	                             (const char *)GAME_PTR(modeAddr)));
}

static void readEntry(FILE *f, StfEntry *stfEntry)
{
	uint32_t i;
	fgets(stfEntry->name, sizeof stfEntry->name, f);
	for (i = 0; i < sizeof stfEntry->name; ++i)
		if (stfEntry->name[i] == '\t' || stfEntry->name[i] == '\n' || stfEntry->name[i] == '\r')
			stfEntry->name[i] = '\0';
	fscanf(f, "%hi\n%d\n%d\n", &stfEntry->car, &stfEntry->time, &stfEntry->mode);
}

REALIGN REGPARM void fetchTrackRecords(MAYBE_THIS uint32_t trackNo, BOOL clear)
{
	/*
	 * From the arena, not the stack: the game writes the path into this, and
	 * it can only address what the arena holds. A local here reached the game
	 * as a truncated host stack address, which it then wrote through.
	 */
	char *buffer = (char *)lowMemAlloc(MAX_PATH);
	Stf stf;
	FILE *f;

	memset(&stf, 0, sizeof stf);

	if (!clear)
	{
		//Get the track records relative file path, +20 means that we want a text file (ssf), not a binary file (stf)
		sub_41B250(trackNo + 20, GAME_ADDR(buffer));
		if ((f = openFile(buffer, "r")))
		{
			//Skip unneeded data
			fgets(buffer, 80, f);
			fscanf(f, "%d\n", (int32_t *)buffer);
			fgets(buffer, 80, f);

			uint32_t i, j;
			readEntry(f, &stf.bestLap); //Best lap
			for (j = 0; j < 3; ++j)
			{
				fgets(buffer, 80, f); //Skip unneeded line
				for (i = 0; i < 10; ++i)
					readEntry(f, &stf.laps[j][i]);
			}

			fclose(f);
		}
	}

	//Get the track records relative file path, get a binary file (stf)
	sub_41B250(trackNo, GAME_ADDR(buffer));
	if ((f = openFile(buffer, "wb")))
	{
		fwrite(&stf, 1, sizeof stf, f);
		fclose(f);
	}

	lowMemFree(buffer);
}
