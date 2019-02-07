#include <stdio.h>
#include <stdlib.h>

#include <git2/errors.h>
#include <git2/global.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/tree.h>

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

int main(int argc, char **argv)
{
	git_repository *repo;
	git_object *obj;

	git_or_die( git_libgit2_init() );
	atexit(cleanup);

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s repo-path\n", *argv);
		return EXIT_FAILURE;
	}
	
	git_or_die( git_repository_open(&repo, argv[1]) );
	git_or_die( git_revparse_single(&obj, repo, "HEAD^{tree}") );

	git_or_die(git_tree_walk(
		(git_tree *)obj, GIT_TREEWALK_POST, pr_node, NULL
	));

	return EXIT_SUCCESS;
}
