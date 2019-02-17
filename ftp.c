#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <git2/blob.h>
#include <git2/commit.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/oid.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/revwalk.h>
#include <git2/tree.h>

#include <sys/socket.h>

#include "socket.h"

#define CLIENT_BUFSZ (10+PATH_MAX)

void git_or_die(FILE *conn, int code)
{
	if (code < 0)
	{
		fprintf(conn, "451 libgit2 error: %s\n", giterr_last()->message);
		exit(EXIT_FAILURE);
	}
}

/* wrapper to match expected atexit type */
void cleanup_git(void)
{
	git_libgit2_shutdown();
}

void ftp_ls(FILE *conn, git_repository *repo, git_tree *tr)
{
	const char *name;
	git_tree_entry *entry;
	git_blob *blob;
	git_tree *past_tr, *sub_tr;
	git_commit *commit;
	git_revwalk *w;

	git_filemode_t mode;
	git_time_t epoch;
	struct tm *tm;
	char timestr[BUFSIZ];
	git_off_t size;
	git_oid commit_oid;
	const git_oid *entry_oid;
	size_t i;

	time_t now = time(NULL);
	int cur_year = localtime(&now)->tm_year;
	
	git_revwalk_new(&w, repo);

	for (i = 0; i < git_tree_entrycount(tr); ++i)
	{
		entry = (git_tree_entry *)git_tree_entry_byindex(tr, i);
		entry_oid = git_tree_entry_id(entry);

		name = git_tree_entry_name(entry);
		mode = git_tree_entry_filemode(entry);

		if (git_tree_entry_type(entry) == GIT_OBJ_TREE) {
			git_tree_lookup(&sub_tr, repo, entry_oid);

			fprintf(conn,
					"drwxr-xr-x   %2zu  git    git      0 %s %s\n",
					git_tree_entrycount(sub_tr), "Jan 01 08:08", name);
		} else {
			git_blob_lookup(&blob, repo, entry_oid);
			size = git_blob_rawsize(blob);

			git_revwalk_reset(w);
			git_revwalk_simplify_first_parent(w);
			git_revwalk_push_head(w);
			while (!git_revwalk_next(&commit_oid, w)) {
				git_commit_lookup(&commit, repo, &commit_oid);
				git_commit_tree(&past_tr, commit);

				if (git_tree_entry_byid(past_tr, entry_oid) != NULL)
					epoch = git_commit_time(commit);
				else
					break;
			}
			tm = localtime((time_t*)&epoch);
			strftime(timestr, sizeof(timestr),
				(tm->tm_year == cur_year)
				? "%b %e %H:%M"
				: "%b %e  %Y"
				, tm);

			fprintf(conn, "-r--r--r--    1  git    git %6lld %s %s\n",
					size, timestr, name);
		}
	}
	git_revwalk_free(w);
}

void pasv_format(const int *ip, int port, char *out)
{
	div_t p = div(port, 256);

	sprintf(out, "(%d,%d,%d,%d,%d,%d)",
			ip[0], ip[1], ip[2], ip[3],
			p.rem, p.quot);
}

void ftp_session(int sock, int *server_ip, const char *gitpath)
{
	char sha[8];
	char cmd[CLIENT_BUFSZ];

	int pasvfd = -1, pasvport;
	FILE *conn, *pasv_conn = NULL;
	char pasv_desc[26]; /* format (%d,%d,%d,%d,%d,%d) */

	git_repository *repo;
	git_object *obj;
	git_tree *tr;

	if ((conn = sock_stream(sock, "a+")) == NULL)
		exit(EXIT_FAILURE);

	git_or_die(conn, git_libgit2_init());
	atexit(cleanup_git);

	git_or_die(conn, git_repository_open(&repo, gitpath) );
	git_or_die(conn, git_revparse_single((git_object **)&obj, repo, "master^{tree}") );
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
			if ((pasv_conn = sock_stream(accept(pasvfd, NULL, NULL), "w")) == NULL)
			{
				fprintf(conn, "452 Failed to accept() pasv sock\n");
				continue;
			}
			fprintf(conn, "150 Opening ASCII mode data connection for file list\n");
			ftp_ls(pasv_conn, repo, tr);
			fclose(pasv_conn);
			pasvfd = -1;
			fprintf(conn, "226 Transfer complete\n");
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
			if (get_ip_port(pasvfd, NULL, &pasvport) < 0)
			{
				close(pasvfd);
				pasvfd = -1;
				fprintf(conn, "452 Passive socket incorrect\n");
				continue;
			}
			pasv_format(server_ip, pasvport, pasv_desc);
			printf("Opening passive socket on %s\n", pasv_desc);

			fprintf(conn, "227 Entering Passive Mode %s\n", pasv_desc);
		}
		else
			fprintf(conn, "502 Unimplemented\n");
	}
	fputs("Client disconnected\n", stderr);
	if (pasv_conn != NULL)
		fclose(pasv_conn);
	git_tree_free(tr);
}
