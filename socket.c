#include "socket.h"

#include <stdio.h>
#include <unistd.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define TCP_BACKLOG  SOMAXCONN

/* adapted from
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/getaddrinfo.html
 *
 * svc: either a name like "ftp" or a port number as string
 *
 * Returns: socket file desciptor, or negative error value
 */
int negotiate_listen(char *svc)
{
	int sock, e, reuseaddr=1;
	struct addrinfo hints = {0}, *addrs, *ap;

	hints.ai_family   = AF_INET;  /* IPv4 required for PASV command */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_TCP;
	if ((e = getaddrinfo(NULL, svc, &hints, &addrs)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		return -1;
	}

	for (ap = addrs; ap != NULL; ap = ap->ai_next)
	{
		sock = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
		if (sock < 0)
			continue;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		               &reuseaddr,sizeof(reuseaddr)) < 0)
			perror("setsockopt(REUSEADDR)");
		if (bind(sock, ap->ai_addr, ap->ai_addrlen) == 0)
			break; /* noice */
		perror("Failed to bind");
		close(sock);
	}
	freeaddrinfo(addrs);

	if (ap == NULL)
	{
		fprintf(stderr, "Could not bind\n");
		return -1;
	}
	if (listen(sock, TCP_BACKLOG) < 0)
	{
		perror("listen()");
		close(sock);
		return -1;
	}

	return sock;
}

int get_ip(int sock, int *ip)
{
	struct sockaddr_in addr = {0};
	socklen_t addr_len = sizeof addr;
	int matched;
	char *dotted;

	if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) != 0)
	{
		perror("getsockname");
		return -1;
	}
	dotted = inet_ntoa(addr.sin_addr);
	matched = sscanf(dotted, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
	if (matched < 4)
	{
		fprintf(stderr, "Unable to parse IPv4 in %s\n", dotted);
		return -1;
	}
	return 0;
}

int get_port(int sock)
{
	struct sockaddr_in addr = {0};
	socklen_t addr_len = sizeof addr;

	if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) != 0)
	{
		perror("getsockname");
		return -1;
	}
	return addr.sin_port;
}

FILE *accept_stream(int sock, char *mode)
{
	int c;
	FILE *ret;
	struct sockaddr addr;
	socklen_t sa_sz = sizeof addr;

	if ((c = accept(sock, &addr, &sa_sz)) < 0)
	{
		perror("accept()");
		return NULL;
	}
	if((ret = fdopen(c, mode)) == NULL)
	{
		perror("fdopen()");
		return NULL;
	}
		
	/* just a preference */
	if (setvbuf(ret, NULL, _IOLBF, BUFSIZ) != 0)
		perror("Warning: unable to change socket buffering");

	return ret;
}
