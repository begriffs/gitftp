#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <git2/errors.h>
#include <git2/global.h>
#include <git2/oid.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/tree.h>

/* choosing a minimal backlog until experience
 * proves that a longer one is advantageous */
#define TCP_BACKLOG  0
#define DEFAULT_PORT "8021"
#define CLIENT_BUFSZ (10+PATH_MAX)

void git_or_die(int code)
{
	if (code < 0)
	{
		fprintf(stderr, "%s\n", giterr_last()->message);
		exit(EXIT_FAILURE);
	}
}

void cleanup(void)
{
	/* probably doesn't matter, but… */
	git_libgit2_shutdown();
}

int pr_node(const char *root, const git_tree_entry *entry, void *payload)
{
	(void)payload;
	printf("%s%s\n", root, git_tree_entry_name(entry));
	return 0;
}

/* adapted from
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/getaddrinfo.html
 *
 * Returns: socket file desciptor, or negative error value
 */
int negotiate_bind(char *svc)
{
	int sock, e;
	struct addrinfo hints = {0}, *addrs, *ap;

	hints.ai_family   = AF_INET;  /* IPv4 required for PASV command */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE | AI_NUMERICSERV;
	hints.ai_protocol = 0;
	if ((e = getaddrinfo(NULL, svc, &hints, &addrs)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		return e;
	}

	for (ap = addrs; ap != NULL; ap = ap->ai_next)
	{
		sock = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
		if (sock < 0)
			continue;
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
		exit(EXIT_FAILURE);
	}

	return sock;
}

int bind_or_die(char *svc)
{
	int sock = negotiate_bind(svc);
	if (sock < 0)
		exit(EXIT_FAILURE);
	return sock;
}

void ftp_ls(FILE *conn, git_tree *tr)
{
	size_t i, n = git_tree_entrycount(tr);
	const char *name;
	git_tree_entry *entry;

	for (i = 0; i < n; ++i)
	{
		 entry = (git_tree_entry *)git_tree_entry_byindex(tr, i);
		 name = git_tree_entry_name(entry);
		 fputs(name, conn);
		 git_tree_entry_free(entry);
	}
}

/* the weird IPv4/port encoding for passive mode
 *
 * returns 0 if desc is filled in properly, else 1
 */
int describe_sock(int sock, char *desc)
{
	struct sockaddr addr = {0};
	struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
	socklen_t addr_len;
	int ip[4];
	div_t port;

 	if (getsockname(sock, &addr, &addr_len) != 0)
	{
		perror("getsockname");
		return 1;
	}
	if (addr.sa_family != AF_INET)
	{
		fputs("Passive socket is not of INET family\n", stderr);
		return -1;
	}

	sscanf(inet_ntoa(addr_in->sin_addr),
	       "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
	port = div(addr_in->sin_port, 256);

	sprintf(desc, "(%d,%d,%d,%d,%d,%d)",
	        ip[0], ip[1], ip[2], ip[3], port.quot, port.rem);
	return 0;
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

void ftp_session(FILE *conn, git_tree *tr)
{
	char sha[8];
	char cmd[CLIENT_BUFSZ];

	int pasvfd;
	FILE *pasv_conn = NULL;
	char pasv_desc[26]; /* format (%d,%d,%d,%d,%d,%d) */

	fprintf(conn, "220 Browsing at SHA (%s)\n",
	        git_oid_tostr(sha, sizeof sha, git_object_id((git_object*)tr)));
	while (fgets(cmd, CLIENT_BUFSZ, conn) != NULL)
	{
		printf("<< %s", cmd);
		if (strncmp(cmd, "USER", 4) == 0)
			fprintf(conn, "331 Username OK, supply any pass\n");
		else if (strncmp(cmd, "PASS", 4) == 0)
			fprintf(conn, "230 Logged in\n");
		else if (strncmp(cmd, "PWD", 3) == 0)
			fprintf(conn, "257 \"/\"\n");
		else if (strncmp(cmd, "CWD", 3) == 0)
			fprintf(conn, "250 Smile and nod\n");
		else if (strncmp(cmd, "NLST", 5) == 0)
			ftp_ls(conn, tr);
		else if (strncmp(cmd, "SYST", 4) == 0)
			fprintf(conn, "215 gitftp\n");
		else if (strncmp(cmd, "TYPE", 4) == 0)
			fprintf(conn, "200 Sure whatever\n");
		else if (strncmp(cmd, "QUIT", 4) == 0)
		{
			fprintf(conn, "250 Bye\n");
			break;
		}
		else if (strncmp(cmd, "PASV", 4) == 0)
		{
			/* ask system for random port */
			pasvfd = negotiate_bind("0");
			if (pasvfd < 0)
			{
				fprintf(conn, "452 Passive mode port unavailable\n");
				continue;
			}
			if (describe_sock(pasvfd, pasv_desc) != 0)
			{
				close(pasvfd);
				fprintf(conn, "452 Passive socket incorrect\n");
				continue;
			}

			if ((pasv_conn = accept_stream(pasvfd, "a+")) == NULL)
			{
				close(pasvfd);
				fprintf(conn, "452 Failed to accept() pasv sock\n");
				continue;
			}
			fprintf(conn, "227 Entering Passive Mode %s\n", pasv_desc);
		}
		else
			fprintf(conn, "502 Unimplemented\n");
	}
	if (pasv_conn != NULL)
		fclose(pasv_conn);
}

void serve(git_tree *tr)
{
	int sock;
	FILE *conn;
	pid_t pid;

	(void)tr;

	sock = bind_or_die(DEFAULT_PORT);

	while (1)
	{
		if ((conn = accept_stream(sock, "a+")) == NULL)
		{
			close(sock);
			exit(EXIT_FAILURE);
		}
		/* just a preference */
		if (setvbuf(conn, NULL, _IOLBF, BUFSIZ) != 0)
			perror("Warning: unable to change socket buffering");

		if ((pid = fork()) < 0)
		{
			fprintf(conn, "452 unable to fork (%s)\n", strerror(errno));
			fclose(conn);
		}
		else if (pid == 0)
		{
			close(sock); /* belongs to parent */
			ftp_session(conn, tr);
			fclose(conn);
			exit(EXIT_SUCCESS);
		}
		fclose(conn); /* let child handle it */
	}
}

int main(int argc, char **argv)
{
	git_repository *repo;
	git_object *obj;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s repo-path\n", *argv);
		return EXIT_FAILURE;
	}

	git_or_die( git_libgit2_init() );
	atexit(cleanup);

	git_or_die( git_repository_open(&repo, argv[1]) );
	git_or_die( git_revparse_single(&obj, repo, "HEAD^{tree}") );

	serve((git_tree *)repo);

	return EXIT_SUCCESS;
}
