#include "utils.h"
#include <string.h>
#include <stdlib.h>

char *strpbrk_or_eos(const char *s,
		const char *accept)
{
	char *p = strpbrk(s, accept);
	if (!p)
		p = strchr(s, '\0');
	return p;
}

char *strdupdelim(const char *beg, const char *end)
{
	char *res = malloc(end - beg + 1);
	memcpy(res, beg, end - beg);
	res[end - beg] = '\0';
	return res;
}


