#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "socket.h"
#include "ftp.h"

#define DEFAULT_PORT "8021"

/* to prevent zombies */
void wait_for_kids(void)
{
	while (wait(NULL) >= 0)
		;
}

int main(int argc, char **argv)
{
	int sock, accepted;
	pid_t pid;
	int ip[4];

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s repo-path\n", *argv);
		return EXIT_FAILURE;
	}

	sock = negotiate_listen(DEFAULT_PORT);
	if (sock < 0)
		exit(EXIT_FAILURE);

	atexit(wait_for_kids);

	while (1)
	{
		if ((accepted = accept(sock, NULL, NULL)) < 0)
		{
			perror("accept()");
			exit(EXIT_FAILURE);
		}
		get_ip_port(accepted, ip, NULL);

		if ((pid = fork()) < 0)
		{
			perror("fork()");
			write(accepted, "452 unable to fork\n", 19);
		}
		else if (pid == 0)
		{
			close(sock); /* belongs to parent */

			ftp_session(accepted, ip, argv[1]);
			exit(EXIT_SUCCESS);
		}
		close(accepted); /* child has its own dup */
	}

	return EXIT_SUCCESS;
}
