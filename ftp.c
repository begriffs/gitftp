#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <git2.h>

#include <sys/socket.h>

#include "path.h"
#include "socket.h"

#define CLIENT_BUFSZ (10+PATH_MAX)

void git_or_die(FILE *conn, int code)
{
	if (code < 0)
	{
		fprintf(conn, "451 libgit2 error: %s\n", git_error_last()->message);
		exit(EXIT_FAILURE);
	}
}

/* wrapper to match expected atexit type */
static void cleanup_git(void)
{
	git_libgit2_shutdown();
}

void ftp_ls(FILE *conn, git_repository *repo, git_tree *tr, git_time_t commit_time)
{
	const char *name;
	git_tree_entry *entry;
	git_blob *blob;
	git_tree *sub_tr;

	git_filemode_t mode;
	struct tm *tm;
	char timestr[BUFSIZ];
	git_off_t size;
	const git_oid *entry_oid;
	size_t i;

	time_t now = time(NULL);
	int cur_year = localtime(&now)->tm_year;

	tm = localtime((time_t*)&commit_time);
	strftime(timestr, sizeof(timestr),
		(tm->tm_year == cur_year)
		? "%b %e %H:%M"
		: "%b %e  %Y"
		, tm);
	
	for (i = 0; i < git_tree_entrycount(tr); ++i)
	{
		entry = (git_tree_entry *)git_tree_entry_byindex(tr, i);
		entry_oid = git_tree_entry_id(entry);

		name = git_tree_entry_name(entry);
		mode = git_tree_entry_filemode(entry);

		if (git_tree_entry_type(entry) == GIT_OBJECT_TREE) {
			git_tree_lookup(&sub_tr, repo, entry_oid);

			fprintf(conn,
					"drwxr-xr-x   %2zu  git    git      0 %s %s\r\n",
					git_tree_entrycount(sub_tr), timestr, name);
			git_tree_free(sub_tr);
		} else {
			git_blob_lookup(&blob, repo, entry_oid);
			size = git_blob_rawsize(blob);
			git_blob_free(blob);

			fprintf(conn, "%s    1  git    git %6lld %s %s\r\n",
					(mode == GIT_FILEMODE_BLOB_EXECUTABLE)
					? "-rwxr-xr-x" : "-rw-r--r--",
					(long long int)size, timestr, name);
		}
	}
}

static int ftp_send(FILE *conn, git_blob *blob, const char *as)
{
	static git_blob_filter_options opt = GIT_BLOB_FILTER_OPTIONS_INIT;
	git_buf buf = GIT_BUF_INIT;
	int status;

	status = git_blob_filter(&buf, blob, as, &opt);
	if (status < 0)
		return status;

	status = fwrite(buf.ptr, buf.size, 1, conn);
	git_buf_dispose(&buf);
	return (status < 1) ? -1 : 0;
}

static void pasv_format(const int *ip, int port, char *out, size_t outlen)
{
	div_t p = div(port, 256);

	snprintf(out, outlen, "(%d,%d,%d,%d,%d,%d)",
			ip[0], ip[1], ip[2], ip[3],
			p.rem, p.quot);
}

static int git_subtree(git_repository *repo, git_tree *root, const char *path, git_tree **sub)
{
	int status;
	git_tree_entry *entry;
	const git_oid *entry_oid;

	status = git_tree_entry_bypath(&entry, root, path);
	if (status != 0)
		return status;
	entry_oid = git_tree_entry_id(entry);

	if (git_tree_entry_type(entry) != GIT_OBJECT_TREE)
	{
		git_tree_entry_free(entry);
		return -1;
	}

	git_tree_lookup(sub, repo, entry_oid);
	git_tree_entry_free(entry);

	return 0;
}

static int git_find_blob(git_repository *repo, git_tree *root, const char *path, git_blob **blob)
{
	int status;
	git_tree_entry *entry;
	git_oid entry_oid;

	status = git_tree_entry_bypath(&entry, root, path);
	if (status != 0)
	{
		git_tree_entry_free(entry);
		return status;
	}
	git_oid_cpy(&entry_oid, git_tree_entry_id(entry));
	git_tree_entry_free(entry);
	return git_blob_lookup(blob, repo, &entry_oid);
}

static void chomp(char *s)
{
	s[strcspn(s, "\n\r")] = '\0';
}

void ftp_session(int sock, int *server_ip, const char *gitpath)
{
	char sha[8];
	char cmd[CLIENT_BUFSZ];
	struct path cur_path, new_path;

	int pasvfd = -1, pasvport;
	FILE *conn, *pasv_conn = NULL;
	char pasv_desc[26]; /* format (%d,%d,%d,%d,%d,%d) */

	git_repository *repo;
	git_commit *ci;
	git_time_t epoch;
	git_tree *root, *cur_dir, *new_dir;
	git_blob *blob;
	git_off_t size;

	if ((conn = sock_stream(sock, "a+")) == NULL)
		exit(EXIT_FAILURE);

	path_init(&cur_path);

	git_or_die(conn, git_libgit2_init());
	atexit(cleanup_git);

	git_or_die(conn, git_repository_open(&repo, gitpath) );
	// try main branch, and fall back to master
	if (git_revparse_single((git_object **)&root, repo, "main^{tree}") != 0)
		git_or_die(conn, git_revparse_single((git_object **)&root, repo, "master^{tree}") );
	if (git_revparse_single((git_object **)&ci, repo, "main^{commit}") != 0)
		git_or_die(conn, git_revparse_single((git_object **)&ci, repo, "master^{commit}") );
	epoch = git_commit_time(ci);
	cur_dir = root;

	fprintf(conn, "220 Browsing at SHA (%s)\n",
	        git_oid_tostr(sha, sizeof sha, git_object_id((git_object*)ci)));

	git_commit_free(ci);

	while (fgets(cmd, CLIENT_BUFSZ, conn) != NULL)
	{
		chomp(cmd);
		printf("<< %s\n", cmd);
		if (strncmp(cmd, "USER", 4) == 0)
			fprintf(conn, "331 Username OK, supply any pass\n");
		else if (strncmp(cmd, "PASS", 4) == 0)
			fprintf(conn, "230 Logged in\n");
		else if (strncmp(cmd, "PWD", 3) == 0)
			fprintf(conn, "257 \"%s\"\n", cur_path.path);
		else if (strncmp(cmd, "CWD", 3) == 0)
		{
			path_cpy(&new_path, &cur_path);
			path_relative(&new_path, cmd+4);

			/* libgit2 can't handle the concept of root */
			if (strcmp(new_path.path, "/") == 0)
			{
				path_init(&cur_path);
				if (cur_dir != root)
					git_tree_free(cur_dir);
				cur_dir = root;
				fprintf(conn, "250 CWD command successful\n");
			}
			/* path+1 to strip the leading slash which freaks libgit2 out */
			else if (git_subtree(repo, root, new_path.path+1, &new_dir) == 0)
			{
				path_cpy(&cur_path, &new_path);
				if (cur_dir != root)
					git_tree_free(cur_dir);
				cur_dir = new_dir;
				fprintf(conn, "250 CWD command successful\n");
			}
			else
				fprintf(conn, "550 %s: No such directory\n", new_path.path);
		}
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
			ftp_ls(pasv_conn, repo, cur_dir, epoch);
			fclose(pasv_conn);
			pasvfd = -1;
			fprintf(conn, "226 Transfer complete\n");
		}
		else if (strncmp(cmd, "RETR", 4) == 0)
		{
			if (pasvfd < 0)
			{
				fprintf(conn, "425 Use PASV first\n");
				continue;
			}
			if ((pasv_conn = sock_stream(accept(pasvfd, NULL, NULL), "w")) == NULL)
			{
				fprintf(conn, "452 Failed to accept() pasv sock\n");
				continue;
			}

			path_cpy(&new_path, &cur_path);
			path_relative(&new_path, cmd+5);

			if (git_find_blob(repo, root, new_path.path+1, &blob) == 0)
			{
				size = git_blob_rawsize(blob);
				fprintf(conn,
				        "150 Opening ASCII mode data connection for %s (%lld bytes).\n",
				        cmd+5, (long long int)size);
				if (ftp_send(pasv_conn, blob, new_path.path+1) < 0)
					fprintf(conn, "426 Transfer error\n");
				else
					fprintf(conn, "226 Transfer complete\n");
				git_blob_free(blob);
				fclose(pasv_conn);
				pasvfd = -1;
			}
			else
				fprintf(conn, "550 %s: No such file\n", new_path.path);
		}
		else if (strncmp(cmd, "SYST", 4) == 0)
			fprintf(conn, "215 UNIX\n");
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
			pasv_format(server_ip, pasvport, pasv_desc, sizeof pasv_desc);
			printf("Opening passive socket on %s\n", pasv_desc);

			fprintf(conn, "227 Entering Passive Mode %s\n", pasv_desc);
		}
		else
			fprintf(conn, "502 Unimplemented\n");
	}
	fputs("Client disconnected\n", stderr);
	if (pasv_conn != NULL)
		fclose(pasv_conn);
	git_tree_free(root);
	git_repository_free(repo);
}
