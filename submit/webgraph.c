#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

#include "webgraph.h"
#include "hash.h"

#define SETBIT(a, n) (a[n/CHAR_BIT] |= (1<<(n%CHAR_BIT)))
#define CLRBIT(a, n) (a[n/CHAR_BIT] &= ~(1<<(n%CHAR_BIT)))
#define GETBIT(a, n) (a[n/CHAR_BIT] & (1<<(n%CHAR_BIT)))

struct webgraph
{
	long size;
	long max_size;

	struct hash_table *url_blacklist;

	const char **url_string;
	struct vec_in_links **in_links_head;
	struct vec_in_links **in_links_tail;
	long *num_out_links;
	unsigned char *dangling_pages;

	double *pr;	

	pthread_mutex_t g_lock;	
};

struct vec_in_links
{
	long url_id;
	struct vec_in_links *next;
};


void vec_in_links_free(struct vec_in_links *l)
{
	while (l)
	{
		struct vec_in_links *next = l->next;
		free(l);
		l = next;
	}
}


webgraph_handle webgraph_new(long size)
{
	struct webgraph *graph;
	unsigned int num_chars;

	graph = (struct webgraph *)calloc(1, sizeof(struct webgraph));

	if (pthread_mutex_init(&graph->g_lock, NULL) != 0)
	{
		free(graph);
		return NULL;
	}

	graph->max_size = size;
	graph->size = 0;

	graph->url_blacklist = make_string_hash_table(size);

	graph->in_links_head = 
		(struct vec_in_links **)calloc(size, 
								sizeof(struct vec_in_links *));

	graph->in_links_tail =
		(struct vec_in_links **)calloc(size,
								sizeof(struct vec_in_links *));

	graph->url_string = (const char **)calloc(size, sizeof(const char *));

	graph->num_out_links = (long *)calloc(size, sizeof(long));
	
	num_chars = size / CHAR_BIT + 1;
	graph->dangling_pages =
		(unsigned char *)malloc(num_chars * sizeof(char));
	memset(graph->dangling_pages, 0xFF, num_chars * sizeof(char)); 
	
	return (webgraph_handle) graph;				 
}

long webgraph_get_size(webgraph_handle handle)
{
	struct webgraph *graph = (struct webgraph *)handle;
	long size;
	
	pthread_mutex_lock(&graph->g_lock);
	size = graph->size;
	pthread_mutex_unlock(&graph->g_lock);
	
	return size;
}

void webgraph_add_link(
						  webgraph_handle handle,
						  const char *dest,
						  const char *src
						 )
{
	struct webgraph *graph = (struct webgraph *)handle;		
	
	long *src_id;
	long *dest_id;

	pthread_mutex_lock(&graph->g_lock);
	
	src_id = hash_table_get(graph->url_blacklist, src);
	dest_id = hash_table_get(graph->url_blacklist, dest);

	assert(src_id != NULL);
	assert(dest_id != NULL);

	if (*src_id < graph->max_size && *dest_id < graph->max_size)
	{
		struct vec_in_links *entry;
		entry = (struct vec_in_links *)
			calloc(1, sizeof(struct vec_in_links));
		entry->url_id = *src_id;
		
		if (graph->in_links_head[*dest_id] == NULL)
			graph->in_links_head[*dest_id] = entry;
		else
			graph->in_links_tail[*dest_id]->next = entry;
		graph->in_links_tail[*dest_id] = entry;

		if (GETBIT(graph->dangling_pages, *src_id))
			CLRBIT(graph->dangling_pages, *src_id);
			
		++graph->num_out_links[*src_id];	

	}

	pthread_mutex_unlock(&graph->g_lock);	
}

int webgraph_contains(
						webgraph_handle handle,
					    char *url
					 )
{
	struct webgraph *graph = (struct webgraph *)handle;
	int ret;

	pthread_mutex_lock(&graph->g_lock);
	ret = hash_table_contains(graph->url_blacklist, url);
	pthread_mutex_unlock(&graph->g_lock);

	return ret;	
}

void webgraph_add_url(
						 webgraph_handle handle,
						 const char *url_string
    				 )
{
	struct webgraph *graph = (struct webgraph *)handle;
	long *id;
	const char *url = strdup(url_string);

	id = (long *)malloc(sizeof(long));

	pthread_mutex_lock(&graph->g_lock);

	if (graph->size >= graph->max_size)
	{
		webgraph_resize(graph, 2 * graph->size);
	}

	*id = graph->size++;
	graph->url_string[*id] = url;
	hash_table_put(graph->url_blacklist, url, id);

	pthread_mutex_unlock(&graph->g_lock);
}

void webgraph_resize(webgraph_handle handle, long size)
{
	struct webgraph *graph = (struct webgraph *)handle;
	long num_chars;
		
	graph->max_size = size;
	
	realloc(graph->url_string, size * sizeof(char *));
	realloc(graph->in_links_head, size * sizeof(struct vec_in_links *));
	realloc(graph->in_links_tail, size * sizeof(struct vec_in_links *));
	realloc(graph->num_out_links, size * sizeof(long));

	num_chars = size / CHAR_BIT + 1;
	realloc(graph->dangling_pages, num_chars * sizeof(char));
}

static int hash_table_cleanup(void *k, void *v, void *dummy)
{
	char *key = (char *)k;
	long *value = (long *)v;
	free(key);
	free(value);
}

void webgraph_delete(webgraph_handle handle)
{
	int i;
	struct webgraph *graph = (struct webgraph *)handle;
	free(graph->url_string);
	
	for (i = 0; i < graph->size; i++)
	{
		vec_in_links_free(graph->in_links_head[i]);
	}
	free(graph->in_links_head);
	free(graph->in_links_tail);
	free(graph->num_out_links);
	free(graph->dangling_pages);

	if (graph->pr)
		free(graph->pr);

	hash_table_for_each(graph->url_blacklist, 
						hash_table_cleanup,
						NULL);

	hash_table_destroy(graph->url_blacklist);	
	
	pthread_mutex_destroy(&graph->g_lock);
	
	free(graph);
}

static void step(webgraph_handle handle, 
				 double *p, 
				 double *p_new,
				 long n,
				 double s)
{
	long i;
	double inner_product = 0.0;
	double sum_p_new = 0.0;

	struct webgraph *graph = (struct webgraph *) handle;
	
	for (i = 0; i < n; i++)
		if (GETBIT(graph->dangling_pages, i))
			inner_product += p[i];
	
	for (i = 0; i < n; i++)
	{
		struct vec_in_links *entry = graph->in_links_head[i];
		double sum = 0.0;
		for (; entry != NULL; entry = entry->next)
			sum += p[entry->url_id] / 
					graph->num_out_links[entry->url_id];
		p_new[i] = s * sum + s * inner_product / n + (1 - s) / n;
		sum_p_new += p_new[i];
	}
	
	for (i = 0; i < n; i++)
		p_new[i] = p_new[i] / sum_p_new;
}

void pagerank(webgraph_handle handle,
              double s, 
              double tolerance)
{
	long i;
	double change;
	double *p_new;
	double *p;

	struct webgraph *graph = (struct webgraph *)handle;

	p = (double *)malloc(graph->size * sizeof(double));
	p_new = (double *)malloc(graph->size * sizeof(double));

	for (i = 0; i < graph->size; i++)
		p[i] = 1 / graph->size;

	change = 2.0;
	
	while (change > tolerance)
	{
		double sum = 0.0;
		double *temp;

		step(graph, p, p_new, graph->size, s);
		for (i = 0; i < graph->size; i++)
			sum += fabs(p[i] - p_new[i]);
		change = sum;

		temp = p;
		p = p_new;
		p_new = temp;
	}

	if (graph->pr)
	{
		free(graph->pr);
	}
	graph->pr = p;

	free(p_new);		
}	

static void heap(const double data[],
		  long index[],
		  const long n,
		  const long m)
{
	int i = m, j, k;
	k = index[i];

	while(i < (n >> 1))
	{
		if ((j = 2 * i + 1) < n - 1
			&& data[index[j]] < data[index[j + 1]])
			j++;
		if (data[k] >= data[index[j]]) break;
		index[i] = index[j];
		i = j;
	}
	index[i] = k;
}

void print_top_n(webgraph_handle handle, long n)
{
	struct webgraph *graph = (struct webgraph *)handle;
	
	long i;
	long index[graph->size];
	long id;

	if (n > graph->size)
		return;

	for (i = 0; i < graph->size; i++) index[i] = i;
	
	for (i = graph->size; i >= 0; i--)
		heap(graph->pr, index, graph->size, i);	
	
	for (i = 0; i < n; i++)
	{
		id = index[0];
		index[0] = index[graph->size - i - 1];
		printf("No.%ld: %s, Pr:%lf, id:%ld\n", i + 1, 
				graph->url_string[id], 
				graph->pr[id],
				id); 
		heap(graph->pr, index, graph->size - i - 1, 0);
	}
}

