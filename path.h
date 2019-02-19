#ifndef GITFTP_PATH_H
#define GITFTP_PATH_H

#include <limits.h>

struct path
{
	char path[PATH_MAX];
	char *up;
};

void path_init(struct path *p);
void path_cpy(struct path *dst, struct path *src);
void path_relative(struct path *p, char *go);

#endif
