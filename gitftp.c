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
	while (git_libgit2_shutdown() > 0)
		;
}

int main(int argc, char **argv)
{
	git_repository *repo;
	git_object *obj;
	git_tree *tree;

	git_or_die( git_libgit2_init() );
	atexit(cleanup);

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s repo-path\n", *argv);
		return EXIT_FAILURE;
	}
	
	git_or_die( git_repository_open(&repo, argv[1]) );
	git_or_die( git_revparse_single(&obj, repo, "HEAD^{tree}") );
	tree = (git_tree*)obj;

	printf("Count: %zu\n", git_tree_entrycount(tree));

	return EXIT_SUCCESS;
}
