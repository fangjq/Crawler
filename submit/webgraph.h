#ifndef _WEBGRAPH_H
#define _WEBGRAPH_H
#include <pthread.h>

typedef void *webgraph_handle;

extern webgraph_handle webgraph_new(long size);

extern long webgraph_get_size(webgraph_handle handle);
#endif
