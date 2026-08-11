// SPDX-License-Identifier: MIT

#ifndef WSOCK32_H
#define WSOCK32_H

#include "Wrapper.h"

#ifndef WIN32
	#include <netinet/tcp.h>
	#include <netinet/in.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <sys/ioctl.h>
	#include <unistd.h>
	#include <string.h>
	#include <netdb.h>
	#include <errno.h>

	struct win_fd_set
	{
		uint32_t fd_count;
		int fd_array[64];
	};

	/*
	 * Returned to the game, which keeps the whole thing at 32-bit widths.
	 * Native pointers would both resize it and hold addresses the game cannot
	 * represent, so the contents are copied below 2 GiB and referred to by
	 * GameAddr. See gethostbyname_wrap.
	 */
	struct win_hostent
	{
		GameAddr h_name;        /* char *  */
		GameAddr h_aliases;     /* char ** */
		int16_t h_addrtype;
		int16_t h_length;
		GameAddr h_addr_list;   /* char ** */
	};
	typedef struct win_hostent win_hostent_t;
	ASSERT_GAME_LAYOUT(win_hostent_t, 16);
#else
	#include <winsock2.h>

	#define win_hostent hostent
	#define win_fd_set fd_set
	#define socklen_t int
#endif

struct sockaddr_ipx
{
	int16_t sa_family;
	int8_t sa_netnum[4];
	int8_t sa_nodenum[6];
	uint16_t sa_socket;
};

#endif // WSOCK32_H
