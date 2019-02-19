#include "path.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static void path_push(struct path *p, char *dir)
{
	char *c, *guard;
	int terminal_slash = p->up[1] == '\0';

	guard = p->path + PATH_MAX - 1;
	for (c = p->up; c < guard && *c; ++c)
		; /* go to end */
	if (!terminal_slash && c < guard)
	{
		p->up = c;
		*c++ = '/';
	}
	while (c < guard && *dir)
		if ((*c++ = *dir++) == '/')
			p->up = c;
	*c = '\0';

	assert(*p->up == '/');
}

static void path_pop(struct path *p)
{
	*p->up = '\0';
	while (p->up > p->path && *p->up != '/')
		p->up--;
	p->up[0] = '/';
	p->up[1] = '\0';

	assert(*p->up == '/');
}

void path_init(struct path *p)
{
	strcpy(p->path, "/");
	p->up = p->path;

	assert(*p->up == '/');
}

void path_cpy(struct path *dst, struct path *src)
{
	strcpy(dst->path, src->path);
	dst->up = dst->path + (src->up - src->path);

	assert(*dst->up == '/');
}

void path_cd(struct path *p, char *go)
{
	char *q;

	if (*go == '/')
	{
		path_init(p);
		go++;
	}

	for (q = strtok(go, "/"); q; q = strtok(NULL, "/"))
	{
		if (strcmp(q, "..") == 0)
			path_pop(p);
		else
			path_push(p, q);
	}

	assert(*p->up == '/');
}
