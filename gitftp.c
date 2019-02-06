#include <stdio.h>
#include <stdlib.h>

#include <git2/errors.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/tree.h>

void diegit(void)
{
	fprintf(stderr, "%s\n", giterr_last()->message);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	git_repository *repo;
	git_object *obj;
	git_tree *tree;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s repo-path\n", *argv);
		return EXIT_FAILURE;
	}
	
	if (git_repository_open(&repo, argv[1]) != 0)
		diegit();
	if (git_revparse_single(&obj, repo, "HEAD^{tree}") != 0)
		diegit();
	tree = (git_tree*)obj;

	printf("Count: %zu\n", git_tree_entrycount(tree));

	return EXIT_SUCCESS;
}
