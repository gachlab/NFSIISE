// SPDX-License-Identifier: MIT

#include "Kernel32.h"
#include "LowMem.h"

#include <SDL2/SDL_timer.h>

void exit_func();

#ifdef WIN32
	extern BOOL useOnlyOneCPU;
#else
	#define ERROR_IO_PENDING 0x3E5
	#define WAIT_TIMEOUT 0x102
	#include <sys/stat.h>
	#include <termios.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <errno.h>

	#ifdef __ANDROID__
		#include <sys/ioctl.h>
		#define tcdrain(fd) \
			ioctl((fd), TCSBRK, 1);
	#endif
#endif

#ifdef WIN32

REALIGN STDCALL void *CreateThread_wrap(void *threadAttributes, uint32_t stackSize, LPTHREAD_START_ROUTINE startAddress, void *parameter, uint32_t creationFlags, uint32_t *threadId)
{
	HANDLE thread = CreateThread(threadAttributes, stackSize, startAddress, parameter, creationFlags, (DWORD *)threadId);
	if (useOnlyOneCPU)
		SetThreadAffinityMask(thread, 1);
	return thread;
}
REALIGN STDCALL uint32_t ResumeThread_wrap(HANDLE hThread)
{
	return ResumeThread(hThread);
}
REALIGN STDCALL BOOL SetThreadPriority_wrap(HANDLE hThread, int priority)
{
	return SetThreadPriority(hThread, priority);
}
REALIGN STDCALL uint32_t GetCurrentThreadId_wrap(void)
{
	return GetCurrentThreadId();
}
REALIGN STDCALL void *GetCurrentThread_wrap(void)
{
	return GetCurrentThread();
}
REALIGN STDCALL BOOL TerminateThread_wrap(HANDLE hThread, uint32_t exitCode)
{
	return false;
}
REALIGN STDCALL void InitializeCriticalSection_wrap(CRITICAL_SECTION *criticalSection)
{
	InitializeCriticalSection(criticalSection);
}
REALIGN STDCALL void EnterCriticalSection_wrap(CRITICAL_SECTION *criticalSection)
{
	EnterCriticalSection(criticalSection);
}
REALIGN STDCALL void LeaveCriticalSection_wrap(CRITICAL_SECTION *criticalSection)
{
	LeaveCriticalSection(criticalSection);
}
REALIGN STDCALL void DeleteCriticalSection_wrap(CRITICAL_SECTION *criticalSection)
{
	DeleteCriticalSection(criticalSection);
}
REALIGN STDCALL void GlobalMemoryStatus_wrap(MEMORYSTATUS *memoryStatus)
{
	GlobalMemoryStatus(memoryStatus);
}
REALIGN STDCALL void ExitProcess_wrap(uint32_t exitCode)
{
	exit_func();
	ExitProcess(exitCode);
}
REALIGN STDCALL uint32_t GetLastError_wrap(void)
{
	return GetLastError();
}
REALIGN STDCALL HANDLE CreateFileA_wrap(const char *fileName, uint32_t desiredAccess, uint32_t shareMode, SECURITY_ATTRIBUTES *securityAttributes, uint32_t creationDisposition, uint32_t flagsAndAttributes, void *templateFile)
{
	char *tmpFileName;
	HANDLE handle;
	if (!strncasecmp(fileName, "\\\\.\\com", 7))
		tmpFileName = strdup(fileName);
	else
		tmpFileName = convertFilePath(fileName, false);
	handle = CreateFileA(tmpFileName, desiredAccess, shareMode, securityAttributes, creationDisposition, flagsAndAttributes, templateFile);
	free(tmpFileName);
	return handle;
}
REALIGN STDCALL HANDLE CreateFileMappingA_wrap(HANDLE hFile, SECURITY_ATTRIBUTES *fileMappingAttributes, uint32_t flProtect, uint32_t dwMaximumSizeHigh, uint32_t dwMaximumSizeLow, const char *lpName)
{
	return CreateFileMappingA(hFile, fileMappingAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
}
REALIGN STDCALL void *MapViewOfFile_wrap(HANDLE fMapping, uint32_t desiredAccess, uint32_t dwFileOffsetHigh, uint32_t dwFileOffsetLow, uint32_t dwNumberOfBytesToMap)
{
	return MapViewOfFile(fMapping, desiredAccess, dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap);
}
REALIGN STDCALL BOOL UnmapViewOfFile_wrap(const void *lpBaseAddress)
{
	return UnmapViewOfFile(lpBaseAddress);
}
REALIGN STDCALL BOOL FlushFileBuffers_wrap(HANDLE hFile)
{
	return FlushFileBuffers(hFile);
}
REALIGN STDCALL BOOL GetOverlappedResult_wrap(HANDLE hFile, OVERLAPPED *overlapped, uint32_t *lpNumberOfBytesTransferred, BOOL bWait)
{
	return GetOverlappedResult(hFile, overlapped, (DWORD *)lpNumberOfBytesTransferred, bWait);
}
REALIGN STDCALL uint32_t GetFileSize_wrap(HANDLE hFile, uint32_t *lpFileSizeHigh)
{
	return GetFileSize(hFile, (DWORD *)lpFileSizeHigh);
}
REALIGN STDCALL BOOL SetEndOfFile_wrap(HANDLE hFile)
{
	return SetEndOfFile(hFile);
}
REALIGN STDCALL uint32_t SetFilePointer_wrap(HANDLE hFile, uint32_t distanceToMove, uint32_t *distanceToMoveHigh, uint32_t moveMethod)
{
	return SetFilePointer(hFile, distanceToMove, (LONG *)distanceToMoveHigh, moveMethod);
}
REALIGN STDCALL BOOL WriteFile_wrap(HANDLE hFile, const void *buffer, uint32_t numberOfBytesToWrite, uint32_t *numberOfBytesWritten, OVERLAPPED *overlapped)
{
	return WriteFile(hFile, buffer, numberOfBytesToWrite, (DWORD *)numberOfBytesWritten, overlapped);
}
REALIGN STDCALL BOOL ReadFile_wrap(HANDLE hFile, void *buffer, uint32_t numberOfBytesToRead, uint32_t *numberOfBytesRead, OVERLAPPED *overlapped)
{
	return ReadFile(hFile, buffer, numberOfBytesToRead, (DWORD *)numberOfBytesRead, overlapped);
}
REALIGN STDCALL BOOL GetCommState_wrap(HANDLE hFile, DCB *dcb)
{
	return GetCommState(hFile, dcb);
}
REALIGN STDCALL BOOL PurgeComm_wrap(HANDLE hFile, uint32_t dwFlags)
{
	return PurgeComm(hFile, dwFlags);
}
REALIGN STDCALL BOOL SetCommState_wrap(HANDLE hFile, DCB *dcb)
{
	return SetCommState(hFile, dcb);
}
REALIGN STDCALL BOOL SetCommTimeouts_wrap(HANDLE hFile, COMMTIMEOUTS *commTimeouts)
{
	return SetCommTimeouts(hFile, commTimeouts);
}
REALIGN STDCALL BOOL DeleteFileA_wrap(const char *fileName)
{
	char *tmpFileName = convertFilePath(fileName, false);
	BOOL ret = DeleteFileA(tmpFileName);
	free(tmpFileName);
	return ret;
}
REALIGN STDCALL void *GetModuleHandleA_wrap(const char *moduleName)
{
	return GetModuleHandleA(moduleName);
}
REALIGN STDCALL BOOL CloseHandle_wrap(void *handle)
{
	return CloseHandle(handle);
}
REALIGN STDCALL HANDLE CreateEventA_wrap(SECURITY_ATTRIBUTES *eventAttributes, BOOL bManualReset, BOOL bInitialState, const char *lpName)
{
	return CreateEventA(eventAttributes, bManualReset, bInitialState, lpName);
}
REALIGN STDCALL BOOL SetEvent_wrap(HANDLE event)
{
	return SetEvent(event);
}
REALIGN STDCALL uint32_t WaitForMultipleObjects_wrap(uint32_t nCount, void *const *lpHandles, BOOL bWaitAll, uint32_t dwMilliseconds)
{
	return WaitForMultipleObjects(nCount, lpHandles, bWaitAll, dwMilliseconds);
}
REALIGN STDCALL BOOL DuplicateHandle_wrap(void *hSourceProcessHandle, void *hSourceHandle, void *hTargetProcessHandle, void **lpTargetHandle, uint32_t desiredAccess, BOOL bInheritHandle, uint32_t dwOptions)
{
	return DuplicateHandle(hSourceProcessHandle, hSourceHandle, hTargetProcessHandle, lpTargetHandle, desiredAccess, bInheritHandle, dwOptions);
}
REALIGN STDCALL void *GetCurrentProcess_wrap(void)
{
	return GetCurrentProcess();
}
REALIGN STDCALL void GetSystemInfo_wrap(SYSTEM_INFO *lpSystemInfo)
{
	return GetSystemInfo(lpSystemInfo);
}
REALIGN STDCALL uint32_t GetCurrentDirectoryA_wrap(uint32_t bufferLength, char *buffer)
{
	return GetCurrentDirectoryA(bufferLength, buffer);
}
REALIGN STDCALL BOOL SetCurrentDirectoryA_wrap(const char *pathName)
{
	char *tmpPathName = convertFilePath(pathName, false);
	BOOL ret = SetCurrentDirectoryA(tmpPathName);
	free(tmpPathName);
	return ret;
}
REALIGN STDCALL BOOL FindNextFileA_wrap(void *findFile, WIN32_FIND_DATAA *findFileData)
{
	return FindNextFileA(findFile, findFileData);
}
REALIGN STDCALL BOOL FindClose_wrap(void *findFile)
{
	return FindClose(findFile);
}
REALIGN STDCALL void *FindFirstFileA_wrap(const char *fileName, WIN32_FIND_DATAA *findFileData)
{
	return FindFirstFileA(fileName, findFileData);
}

#else /* Linux functions */

static uint32_t overlapped_error;

extern char *serialPort[4];
extern SDL_mutex *event_mutex;
extern SDL_cond *event_cond;

static int threadFunction(void *data)
{
	Thread *thread = (Thread *)data;
	SDL_SemWait(thread->sem);
	SDL_DestroySemaphore(thread->sem);
#ifdef NFS_CPP
	thread->function(thread->arg);
#else
	thread->threadParameter(); //In this game thread parameter is a function address
#endif
	return 0;
}

/**/

REALIGN STDCALL GameAddr CreateThread_wrap(GameAddr threadAttributes, uint32_t stackSize, THREAD_START_ROUTINE startAddress, GameAddr parameterAddr, uint32_t creationFlags, GameAddr threadIdAddr)
{
	void *parameter = GAME_PTR(parameterAddr);
	uint32_t *threadId = (uint32_t *)GAME_PTR(threadIdAddr);
	Thread *thread = (Thread *)lowMemAlloc(sizeof(Thread));
	thread->handleType = HandleThread;
#ifdef NFS_CPP
	thread->function = startAddress;
	thread->arg = parameter;
#else
	thread->threadParameter = parameter;
#endif
	thread->sem = SDL_CreateSemaphore(!(creationFlags & 0x4 /* Start paused thread */));
	SDL_Thread *sdl_thread = SDL_CreateThread(threadFunction, NULL, thread);
	if (threadId)
		*threadId = SDL_GetThreadID(sdl_thread);
	SDL_DetachThread(sdl_thread);
	return GAME_ADDR(thread);
}
REALIGN STDCALL uint32_t ResumeThread_wrap(GameAddr threadAddr)
{
	Thread *thread = (Thread *)GAME_PTR(threadAddr);
	SDL_SemPost(thread->sem);
	return 0;
}
REALIGN STDCALL BOOL SetThreadPriority_wrap(GameAddr threadAddr, int priority)
{
	Thread *thread = (Thread *)GAME_PTR(threadAddr);
	return false;
}
REALIGN STDCALL uint32_t GetCurrentThreadId_wrap(void)
{
	return SDL_ThreadID();
}
REALIGN STDCALL GameAddr GetCurrentThread_wrap(void)
{
	return 0;
}
REALIGN STDCALL BOOL TerminateThread_wrap(GameAddr threadAddr, uint32_t exitCode)
{
	Thread *thread = (Thread *)GAME_PTR(threadAddr);
	return false;
}
REALIGN STDCALL void InitializeCriticalSection_wrap(GameAddr criticalSectionAddr)
{
	CRITICAL_SECTION *criticalSection = (CRITICAL_SECTION *)GAME_PTR(criticalSectionAddr);
	criticalSection->mutex = (uint64_t)(uintptr_t)SDL_CreateMutex();
}
REALIGN STDCALL void EnterCriticalSection_wrap(GameAddr criticalSectionAddr)
{
	CRITICAL_SECTION *criticalSection = (CRITICAL_SECTION *)GAME_PTR(criticalSectionAddr);
	SDL_LockMutex((SDL_mutex *)(uintptr_t)criticalSection->mutex);
}
REALIGN STDCALL void LeaveCriticalSection_wrap(GameAddr criticalSectionAddr)
{
	CRITICAL_SECTION *criticalSection = (CRITICAL_SECTION *)GAME_PTR(criticalSectionAddr);
	SDL_UnlockMutex((SDL_mutex *)(uintptr_t)criticalSection->mutex);
}
REALIGN STDCALL void DeleteCriticalSection_wrap(GameAddr criticalSectionAddr)
{
	CRITICAL_SECTION *criticalSection = (CRITICAL_SECTION *)GAME_PTR(criticalSectionAddr);
	SDL_DestroyMutex((SDL_mutex *)(uintptr_t)criticalSection->mutex);
	criticalSection->mutex = 0;
}

REALIGN STDCALL void GlobalMemoryStatus_wrap(GameAddr memoryStatusAddr)
{
	MEMORYSTATUS *memoryStatus = (MEMORYSTATUS *)GAME_PTR(memoryStatusAddr);
	memset(memoryStatus, 0, sizeof(MEMORYSTATUS));
	memoryStatus->length = sizeof(MEMORYSTATUS);
	memoryStatus->memoryLoad = 0;
	memoryStatus->totalPhys = 0x7FFFFFFF;
	memoryStatus->totalPageFile = 0x7FFFFFFF;
	memoryStatus->availPageFile = 0x7FFFFFFF;
	memoryStatus->totalVirtual = 0x7FFFFFFF;
	memoryStatus->availVirtual = 0x7FFFFFFF;
}

REALIGN STDCALL void ExitProcess_wrap(uint32_t exitCode)
{
	exit_func();
	exit(exitCode);
}

REALIGN STDCALL uint32_t GetLastError_wrap(void)
{
	if (overlapped_error)
	{
		uint32_t e = overlapped_error;
		overlapped_error = 0;
		return e;
	}
	switch (errno)
	{
		case ENOENT:
			return errno;
	}
	return 0;
}

REALIGN STDCALL GameAddr CreateEventA_wrap(GameAddr eventAttributes, BOOL manualReset, BOOL initialState, GameAddr name)
{
	Event *event = (Event *)lowMemAlloc(sizeof(Event));
	event->handleType = HandleEvent;
	event->manualReset = manualReset;
	event->is_set = initialState;
	return GAME_ADDR(event);
}
REALIGN STDCALL BOOL SetEvent_wrap(GameAddr eventAddr)
{
	Event *event = (Event *)GAME_PTR(eventAddr);
	if (event)
	{
		SDL_LockMutex(event_mutex);
		event->is_set = true;
		SDL_CondBroadcast(event_cond);
		SDL_UnlockMutex(event_mutex);
		return true;
	}
	return false;
}
/*
 * The game passes an array of handles, and its handles are 4 bytes each.
 * Indexing it as Event *const * strides by 8 in a 64-bit build, so entry 1
 * onwards is read from the wrong place and entry 0 comes back with garbage in
 * its high half. Read the array at the game's element width instead.
 */
REALIGN STDCALL uint32_t WaitForMultipleObjects_wrap(uint32_t count, const GameAddr *events, BOOL waitAll, uint32_t milliseconds)
{
	//milliseconds always -1 or 0
	//waitAll always false
	uint32_t i, ret = WAIT_TIMEOUT;
	SDL_LockMutex(event_mutex);
	for (;;)
	{
		for (i = 0; i != count; ++i)
		{
			Event *event = (Event *)GAME_PTR(events[i]);
			if (event->is_set)
			{
				if (ret == WAIT_TIMEOUT)
					ret = i;
				if (!event->manualReset)
					event->is_set = false;
			}
		}
		if (ret != WAIT_TIMEOUT || !milliseconds)
			break;
		if (SDL_CondWait(event_cond, event_mutex) == -1) //no timeout, because milliseconds will be always -1 here
		{
			ret = -1;
			break;
		}
	}
	SDL_UnlockMutex(event_mutex);
	return ret;
}

static int serialPortThread(void *data)
{
	File *file = (File *)data;
	struct timeval tv;
	int bread, r;
	fd_set fds;
	for (;;)
	{
		FD_ZERO(&fds);
		FD_SET(file->fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = file->us_timeout;
		r = select(file->fd + 1, &fds, NULL, NULL, &tv);
		SDL_LockMutex(file->mutex);
		if (r < 0)
		{
			file->pending = false;
			SDL_UnlockMutex(file->mutex);
			break;
		}
		else if (r == 1)
		{
			if ((bread = read(file->fd, file->asyncReadBuffer, file->toRead)) > 0)
			{
				if ((file->toRead -= bread))
					file->asyncReadBuffer += bread;
				else
					file->pending = false;
				file->readSoFar += bread;
			}
			else if (bread < 0 || !file->toRead) //is it necessary?
			{
				file->pending = false;
				SDL_UnlockMutex(file->mutex);
				break;
			}
		}
		SetEvent_wrap(file->readOverlapped->hEvent);
		if (!file->pending)
		{
			SDL_UnlockMutex(file->mutex);
			break;
		}
		SDL_UnlockMutex(file->mutex);
	}
	return 0;
}

REALIGN STDCALL GameAddr CreateFileA_wrap(GameAddr fileNameAddr, uint32_t desiredAccess, uint32_t shareMode, GameAddr securityAttributes, uint32_t creationDisposition, uint32_t flagsAndAttributes, GameAddr templateFile)
{
	const char *fileName = (const char *)GAME_PTR(fileNameAddr);
	uint32_t COM_number = 0;
	if (!strncasecmp(fileName, "\\\\.\\com", 7))
		COM_number = fileName[7] - '0';
	char *tmpFileName = COM_number ? strdup(serialPort[COM_number - 1]) : convertFilePath(fileName, true);

	File *file = NULL;
	int fd = -1;
	switch (desiredAccess)
	{
		case GENERIC_READ | GENERIC_WRITE:
			if (COM_number)
				fd = open(tmpFileName, O_RDWR | O_NOCTTY | O_NDELAY);
			break;
		case GENERIC_WRITE:
			fd = open(tmpFileName, O_CREAT | O_WRONLY | O_TRUNC, 0644);
			break;
		case GENERIC_READ:
			fd = open(tmpFileName, O_RDONLY);
			break;
	}
	if (fd > 0)
	{
		file = lowMemCalloc(1, sizeof(File));
		file->handleType = HandleFile;
		if ((file->async = !!(flagsAndAttributes & 0x40000000 /* Overlapped, async mode */)))
			file->mutex = SDL_CreateMutex();
		file->fd = fd;
	}

	free(tmpFileName);

	return file ? GAME_ADDR(file) : (GameAddr)-1;
}
REALIGN STDCALL uint32_t GetFileSize_wrap(GameAddr fileAddr, GameAddr fileSizeHighAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	uint32_t *fileSizeHigh = (uint32_t *)GAME_PTR(fileSizeHighAddr);
	struct stat stat;
	if (!fstat(file->fd, &stat))
		return stat.st_size;
	return -1;
}
REALIGN STDCALL GameAddr CreateFileMappingA_wrap(GameAddr fileAddr, GameAddr fileMappingAttributes, uint32_t protect, uint32_t maximumSizeHigh, uint32_t maximumSizeLow, GameAddr name)
{
	File *file = (File *)GAME_PTR(fileAddr);
	FileMapping *fileMapping = (FileMapping *)lowMemAlloc(sizeof(FileMapping));
	fileMapping->handleType = HandleFileMapping;
	fileMapping->fd = file->fd;
	return GAME_ADDR(fileMapping);
}
REALIGN STDCALL GameAddr MapViewOfFile_wrap(GameAddr fMappingAddr, uint32_t desiredAccess, uint32_t fileOffsetHigh, uint32_t fileOffsetLow, uint32_t numberOfBytesToMap)
{
	FileMapping *fMapping = (FileMapping *)GAME_PTR(fMappingAddr);
	//Cannot use mmap() because UnmapViewOfFile doesn't provide the size. I don't want to use an array for sizes. This is also OK.
	uint32_t size = GetFileSize_wrap(GAME_ADDR(fMapping), 0);
	void *fileMap = NULL;
	if (size > 0)
	{
		off_t pos = lseek(fMapping->fd, 0, SEEK_CUR);
		lseek(fMapping->fd, 0, SEEK_SET);
		fileMap = lowMemAlloc(size + 4);
		read(fMapping->fd, fileMap, size);
		lseek(fMapping->fd, pos, SEEK_SET);
	}
	return GAME_ADDR(fileMap);
}
REALIGN STDCALL BOOL UnmapViewOfFile_wrap(GameAddr lpBaseAddress)
{
	lowMemFree((void *)lpBaseAddress);
	return true;
}
REALIGN STDCALL BOOL FlusfileBuffers_wrap(GameAddr fileAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	return !fsync(file->fd);
}
REALIGN STDCALL BOOL GetOverlappedResult_wrap(GameAddr fileAddr, GameAddr overlappedAddr, GameAddr lpNumberOfBytesTransferredAddr, BOOL bWait)
{
	File *file = (File *)GAME_PTR(fileAddr);
	OVERLAPPED *overlapped = (OVERLAPPED *)GAME_PTR(overlappedAddr);
	uint32_t *lpNumberOfBytesTransferred = (uint32_t *)GAME_PTR(lpNumberOfBytesTransferredAddr);
	SDL_LockMutex(file->mutex);
	if (file->readOverlapped == overlapped)
	{
		if ((*lpNumberOfBytesTransferred = file->readSoFar) > 0)
		{
			SDL_UnlockMutex(file->mutex);
			return true;
		}
	}
	SDL_UnlockMutex(file->mutex);
	return false;
}
REALIGN STDCALL BOOL SetEndOfFile_wrap(GameAddr fileAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	return !lseek(file->fd, 0, SEEK_END);
}
REALIGN STDCALL uint32_t SetFilePointer_wrap(GameAddr fileAddr, uint32_t distanceToMove, GameAddr distanceToMoveHighAddr, uint32_t moveMethod)
{
	File *file = (File *)GAME_PTR(fileAddr);
	uint32_t *distanceToMoveHigh = (uint32_t *)GAME_PTR(distanceToMoveHighAddr);
	return lseek(file->fd, distanceToMove, moveMethod);
}
REALIGN STDCALL BOOL WriteFile_wrap(GameAddr fileAddr, GameAddr bufferAddr, uint32_t numberOfBytesToWrite, GameAddr numberOfBytesWrittenAddr, GameAddr overlappedAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	const void *buffer = GAME_PTR(bufferAddr);
	uint32_t *numberOfBytesWritten = (uint32_t *)GAME_PTR(numberOfBytesWrittenAddr);
	OVERLAPPED *overlapped = (OVERLAPPED *)GAME_PTR(overlappedAddr);
	BOOL hasEvent = file->async && overlapped && overlapped->hEvent, ret;
	if (hasEvent)
	{
		SDL_LockMutex(event_mutex);
		((Event *)GAME_PTR(overlapped->hEvent))->is_set = false;
		SDL_UnlockMutex(event_mutex);
	}
	*numberOfBytesWritten = write(file->fd, buffer, numberOfBytesToWrite);
	ret = numberOfBytesToWrite == *numberOfBytesWritten;
	if (hasEvent && ret)
	{
		tcdrain(file->fd);
		SetEvent_wrap(overlapped->hEvent);
	}
	return ret;
}
REALIGN STDCALL BOOL ReadFile_wrap(GameAddr fileAddr, GameAddr bufferAddr, uint32_t numberOfBytesToRead, GameAddr numberOfBytesReadAddr, GameAddr overlappedAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	void *buffer = GAME_PTR(bufferAddr);
	uint32_t *numberOfBytesRead = (uint32_t *)GAME_PTR(numberOfBytesReadAddr);
	OVERLAPPED *overlapped = (OVERLAPPED *)GAME_PTR(overlappedAddr);
	if (file->async)
	{
		if (overlapped && overlapped->hEvent)
		{
			SDL_LockMutex(file->mutex);

			SDL_LockMutex(event_mutex);
			((Event *)GAME_PTR(overlapped->hEvent))->is_set = false;
			SDL_UnlockMutex(event_mutex);

			file->asyncReadBuffer = buffer;
			file->readOverlapped = overlapped;
			file->toRead = numberOfBytesToRead;
			file->readSoFar = 0;

			if (!file->pending)
			{
				file->pending = true;
				SDL_DetachThread(SDL_CreateThread(serialPortThread, NULL, file));
			}
			overlapped_error = ERROR_IO_PENDING;

			SDL_UnlockMutex(file->mutex);
		}
		*numberOfBytesRead = 0;
		return false;
	}
	*numberOfBytesRead = read(file->fd, buffer, numberOfBytesToRead);
	return *numberOfBytesRead == numberOfBytesToRead;
}
REALIGN STDCALL BOOL GetCommState_wrap(GameAddr fileAddr, GameAddr dcbAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	DCB *dcb = (DCB *)GAME_PTR(dcbAddr);
	return true;
}
REALIGN STDCALL BOOL PurgeComm_wrap(GameAddr fileAddr, uint32_t flags)
{
	File *file = (File *)GAME_PTR(fileAddr);
	if (flags & 0x5)
		tcflush(file->fd, TCOFLUSH);
	if (flags & 0xA)
		tcflush(file->fd, TCIFLUSH);
	return true;
}
REALIGN STDCALL BOOL SetCommState_wrap(GameAddr fileAddr, GameAddr dcbAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	DCB *dcb = (DCB *)GAME_PTR(dcbAddr);
	struct termios tty;
	memset(&tty, 0, sizeof(struct termios));
	cfsetospeed(&tty, B9600);
	cfsetispeed(&tty, B9600);
	tty.c_iflag |= IGNBRK;
	tty.c_cflag |= CS8 | CLOCAL | CREAD;
	return !tcsetattr(file->fd, TCSANOW, &tty);
}
REALIGN STDCALL BOOL SetCommTimeouts_wrap(GameAddr fileAddr, GameAddr commTimeoutsAddr)
{
	File *file = (File *)GAME_PTR(fileAddr);
	COMMTIMEOUTS *commTimeouts = (COMMTIMEOUTS *)GAME_PTR(commTimeoutsAddr);
	file->us_timeout = commTimeouts->ReadTotalTimeoutConstant * 1000;
	return true;
}

REALIGN STDCALL BOOL DeleteFileA_wrap(GameAddr fileNameAddr)
{
	const char *fileName = (const char *)GAME_PTR(fileNameAddr);
	char *tmpFileName = convertFilePath(fileName, true);
	BOOL ret = !unlink(tmpFileName);
	free(tmpFileName);
	return ret;
}

REALIGN STDCALL GameAddr GetModuleHandleA_wrap(GameAddr moduleName)
{
	return 0;
}
REALIGN STDCALL BOOL CloseHandle_wrap(GameAddr handleAddr)
{
	void *handle = GAME_PTR(handleAddr);
	if (!handle)
		return false;
	switch (*(HandleType *)handle)
	{
		case HandleThread:
		{
			Thread *thread = (Thread *)handle;
			lowMemFree(thread);
			return true;
		}
		case HandleFile:
		{
			File *file = (File *)handle;
			SDL_LockMutex(file->mutex);
			close(file->fd);
			SDL_UnlockMutex(file->mutex);
			while (file->pending) //Cannot wait for finished, because thread is detached
				SDL_Delay(10);
			SDL_DestroyMutex(file->mutex);
			lowMemFree(file);
			return true;
		}
		case HandleFileMapping:
		{
			FileMapping *fMapping = (FileMapping *)handle;
			lowMemFree(fMapping);
			return true;
		}
		case HandleEvent:
		{
			Event *event = (Event *)handle;
			lowMemFree(event);
			return true;
		}
	}
	return false;
}

REALIGN STDCALL BOOL DuplicateHandle_wrap(GameAddr hSourceProcessHandle, GameAddr hSourceHandle, GameAddr hTargetProcessHandle, GameAddr lpTargetHandleAddr, uint32_t desiredAccess, BOOL bInheritHandle, uint32_t dwOptions)
{
	GameAddr *lpTargetHandle = (GameAddr *)GAME_PTR(lpTargetHandleAddr);
	*lpTargetHandle = 0;
	return false;
}

REALIGN STDCALL GameAddr GetCurrentProcess_wrap(void)
{
	return 0;
}

REALIGN STDCALL void GetSystemInfo_wrap(GameAddr systemInfoAddr)
{
	SYSTEM_INFO *systemInfo = (SYSTEM_INFO *)GAME_PTR(systemInfoAddr);
	memset(systemInfo, 0, sizeof(SYSTEM_INFO));
	systemInfo->pageSize = getpagesize();
}

REALIGN STDCALL uint32_t GetCurrentDirectoryA_wrap(uint32_t bufferLength, GameAddr bufferAddr)
{
	char *buffer = (char *)GAME_PTR(bufferAddr);
	if (getcwd(buffer, bufferLength))
		return bufferLength;
	return 0;
}
REALIGN STDCALL BOOL SetCurrentDirectoryA_wrap(GameAddr pathNameAddr)
{
	const char *pathName = (const char *)GAME_PTR(pathNameAddr);
	char *tmpPathName = convertFilePath(pathName, false);
	BOOL ret = !chdir(tmpPathName);
	free(tmpPathName);
	return ret;
}

REALIGN STDCALL BOOL FindNextFileA_wrap(GameAddr findFileAddr, GameAddr findFileDataAddr)
{
	FindFile *findFile = (FindFile *)GAME_PTR(findFileAddr);
	WIN32_FIND_DATA *findFileData = (WIN32_FIND_DATA *)GAME_PTR(findFileDataAddr);
	struct dirent *de;
	int pos;
	while ((de = readdir(findFile->dir)))
	{
		if (*de->d_name == '.' || de->d_type == DT_DIR)
			continue;
		pos = strlen(de->d_name) - strlen(findFile->filter + 1);
		if (pos < 0 || strcasecmp(de->d_name + pos, findFile->filter + 1))
			continue;
		strcpy(findFileData->fileName, de->d_name);
		return true;
	}
	return false;
}
REALIGN STDCALL BOOL FindClose_wrap(GameAddr findFileAddr)
{
	FindFile *findFile = (FindFile *)GAME_PTR(findFileAddr);
	if (findFile)
	{
		if (findFile->dir)
			closedir(findFile->dir);
		free(findFile->filter);
		lowMemFree(findFile);
		return true;
	}
	return false;
}
REALIGN STDCALL GameAddr FindFirstFileA_wrap(GameAddr fileNameAddr, GameAddr findFileDataAddr)
{
	const char *fileName = (const char *)GAME_PTR(fileNameAddr);
	WIN32_FIND_DATA *findFileData = (WIN32_FIND_DATA *)GAME_PTR(findFileDataAddr);
	memset(findFileData, 0, sizeof(WIN32_FIND_DATA));

	if (*fileName != '*') //This condition should be always false
		return (GameAddr)-1;

	DIR *dir = opendir(".");
	if (!dir)
		return (GameAddr)-1;

	FindFile *findFile = (FindFile *)lowMemAlloc(sizeof(FindFile));
	findFile->dir = dir;
	findFile->filter = strdup(fileName);

	if (!FindNextFileA_wrap(GAME_ADDR(findFile), GAME_ADDR(findFileData)))
	{
		FindClose_wrap(GAME_ADDR(findFile));
		return (GameAddr)-1;
	}

	return GAME_ADDR(findFile);
}

#endif
