#ifndef _UTILS_H
#define _UTILS_H

#define MIN(i, j) ((i) <= (j) ? (i) : (j))

#define DO_REALLOC(basevar, sizevar, needed_size, type) do{   \
	long DR_needed_size = (needed_size);                      \
	long DR_newsize = 0;                           		      \
	while ((sizevar) < (DR_needed_size)){                     \
		DR_newsize = sizevar << 1; 						      \
		if (DR_newsize < 16)                                  \
			DR_newsize = 16;                                  \
		(sizevar) = DR_newsize; 						      \
	}                                                         \
	if (DR_newsize)                                           \
		basevar = realloc(basevar, DR_newsize * sizeof(type));\
}while (0)                                                    

extern char *strpbrk_or_eos(const char *s,
		const char *accept);

extern char *strdupdelim(const char *beg, const char *end);


#endif
