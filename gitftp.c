#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netdb.h>

#include <git2/errors.h>
#include <git2/global.h>
#include <git2/oid.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/tree.h>

#define DEFAULT_PORT "8021"
#define TCP_BACKLOG  0

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
 */
int bind_or_die(char *svc)
{
	int sock, e;
	struct addrinfo hints = {0}, *addrs, *ap;

	hints.ai_family   = AF_UNSPEC;  /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE; /* have system provide IP */
	hints.ai_protocol = 0;
	if ((e = getaddrinfo(NULL, svc, &hints, &addrs)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		exit(EXIT_FAILURE);
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

	if (ap == NULL)
	{
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(addrs);

	return sock;
}

void serve(git_tree *tr)
{
	int sock, c;
	FILE *conn;
	struct sockaddr client;
	socklen_t clientsz = sizeof client;
	pid_t pid;
	char sha[8];

	(void)tr;

	sock = bind_or_die(DEFAULT_PORT);

	/* choosing a minimal backlog until experience
	 * proves that a longer one is advantageous */
	if (listen(sock, TCP_BACKLOG) < 0)
	{
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	while (1)
	{
		if ((c = accept(sock, &client, &clientsz)) < 0 ||
		    (conn = fdopen(c, "r+")) == NULL)
		{
			perror("Failed accepting connection");
			close(sock);
			exit(EXIT_FAILURE);
		}
		/* just a preference */
		setvbuf(conn, NULL, _IOLBF, BUFSIZ);

		pid = fork();
		if (pid < 0)
		{
			fprintf(conn, "452 unable to fork (%s)\n", strerror(errno));
			fclose(conn);
		}
		else if (pid == 0)
		{
			close(sock); /* belongs to parent */
			fprintf(stderr, "Welcoming client\n");
			fprintf(conn, "220 SHA (%s)\n",
			        git_oid_tostr(sha, sizeof sha, git_object_id((git_object*)tr)));

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
