#include "stat.h"
#include <pthread.h>

static long cur_urlid = 0;

static pthread_mutex_t id_lock = PTHREAD_MUTEX_INITIALIZER;

long *get_cur_urlid()
{
	long *ret = NULL;
	ret = (long *)malloc(sizeof(long));
	pthread_mutex_lock(&id_lock);
	*ret = cur_urlid;
	++cur_urlid;
	pthread_mutex_unlock(&id_lock);
	return ret;
}
