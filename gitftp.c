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
#include <sys/wait.h>

#include <git2/errors.h>
#include <git2/global.h>
#include <git2/oid.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/tree.h>

#include "socket.h"

#define DEFAULT_PORT "8021"
#define CLIENT_BUFSZ (10+PATH_MAX)

void git_or_die(FILE *conn, int code)
{
	if (code < 0)
	{
		fprintf(conn, "451 libgit2 error: %s\n", giterr_last()->message);
		exit(EXIT_FAILURE);
	}
}

int listen_or_die(char *svc)
{
	int sock = negotiate_listen(svc);
	if (sock < 0)
		exit(EXIT_FAILURE);
	return sock;
}


/* wrapper to match expected atexit type */
void cleanup_git(void)
{
	git_libgit2_shutdown();
}

struct pid_list
{
	pid_t pid;
	struct pid_list *next;
};
struct pid_list *g_kids = NULL;

/* wait to prevent zombies */
void wait_for_kids(void)
{
	int status;
	struct pid_list *k;

	for (k = g_kids; k; k = k->next)
		waitpid(k->pid, &status, 0);
}

void add_waitlist(pid_t k)
{
	struct pid_list *head = malloc(sizeof(struct pid_list));

	head->pid = k;
	head->next = g_kids;
	g_kids = head;
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
		 fprintf(conn, "%s\n", name);
		 git_tree_entry_free(entry);
	}

	fputs("file1.txt\n", conn);
	fputs("file2.txt\n", conn);
}

void pasv_format(int *ip, int port, char *out)
{
	div_t p = div(port, 256);

	sprintf(out, "(%d,%d,%d,%d,%d,%d)",
			ip[0], ip[1], ip[2], ip[3],
			p.rem, p.quot);
}

void ftp_session(FILE *conn, int ip[4], char *gitpath)
{
	char sha[8];
	char cmd[CLIENT_BUFSZ];

	int pasvfd = -1;
	FILE *pasv_conn = NULL;
	char pasv_desc[26]; /* format (%d,%d,%d,%d,%d,%d) */

	git_repository *repo;
	git_object *obj;
	git_tree *tr;

	git_or_die(conn, git_repository_open(&repo, gitpath) );
	git_or_die(conn, git_revparse_single((git_object **)&obj, repo, "HEAD^{tree}") );
	tr = (git_tree *)obj;

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
		else if (strncmp(cmd, "LIST", 4) == 0)
		{
			if (pasvfd < 0)
			{
				fprintf(conn, "425 Use PASV first\n");
				continue;
			}

			puts("Listing requested, accepting");
			if ((pasv_conn = accept_stream(pasvfd, "w")) == NULL)
			{
				fprintf(conn, "452 Failed to accept() pasv sock\n");
				continue;
			}
			fprintf(conn, "150 Opening ASCII mode data connection for file list\n");
			ftp_ls(pasv_conn, tr);
			fclose(pasv_conn);
			pasvfd = -1;
			fprintf(conn, "226 Directory finished\n");
		}
		else if (strncmp(cmd, "SYST", 4) == 0)
			fprintf(conn, "215 git\n");
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
			pasvfd = negotiate_listen("0");
			if (pasvfd < 0)
			{
				fprintf(conn, "452 Passive mode port unavailable\n");
				continue;
			}
			if (describe_sock(pasvfd, ip, pasv_desc) != 0)
			{
				close(pasvfd);
				pasvfd = -1;
				fprintf(conn, "452 Passive socket incorrect\n");
				continue;
			}
			printf("Opening passive socket on %s\n", pasv_desc);

			fprintf(conn, "227 Entering Passive Mode %s\n", pasv_desc);
		}
		else
			fprintf(conn, "502 Unimplemented\n");
	}
	if (pasv_conn != NULL)
		fclose(pasv_conn);
}

int main(int argc, char **argv)
{
	int sock;
	FILE *conn;
	pid_t pid;

	int ip[4];
	struct sockaddr_in addr = {0};
	socklen_t addr_len = sizeof addr;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s repo-path\n", *argv);
		return EXIT_FAILURE;
	}

	sock = listen_or_die(DEFAULT_PORT);

	atexit(wait_for_kids);

	while (1)
	{
		if ((conn = accept_stream(sock, "a+")) == NULL)
		{
			close(sock);
			exit(EXIT_FAILURE);
		}
		if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) != 0)
		{
			perror("getsockname");
			exit(EXIT_FAILURE);
		}
		sscanf(inet_ntoa(addr.sin_addr),
			   "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);

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

			git_or_die(conn, git_libgit2_init());
			atexit(cleanup_git);

			ftp_session(conn, ip, argv[1]);
			fclose(conn);
			exit(EXIT_SUCCESS);
		}
		add_waitlist(pid);
		fclose(conn); /* let child handle it */
	}

	return EXIT_SUCCESS;
}
