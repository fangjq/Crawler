#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

#include "threadpool.h"
#include "http.h"
#include "url.h"
#include "hash.h"
#include "webgraph.h"

#define NUM_THREADS 200

#define SETBIT(a, n) (a[n/CHAR_BIT] |= (1<<(n%CHAR_BIT)))
#define CLRBIT(a, n) (a[n/CHAR_BIT] &= ~(1<<(n%CHAR_BIT)))
#define GETBIT(a, n) (a[n/CHAR_BIT] & (1<<(n%CHAR_BIT)))


static struct url_queue *queue = NULL; 

static threadpool pool;

static webgraph_handle graph;

static struct
{
	char queue_empty[NUM_THREADS / CHAR_BIT + 1];
	pthread_mutex_t f_lock;
}flag = {{0}, PTHREAD_MUTEX_INITIALIZER};

static int check_exit()
{
	int i;
	int exit = 1;
	
	pthread_mutex_lock(&flag.f_lock);

	for (i = 0; i < NUM_THREADS; i++)
		if (GETBIT(flag.queue_empty, i) == 0)
			exit = 0;

	pthread_mutex_unlock(&flag.f_lock);

	return exit;
}

void retrieve_webpage(void *arg)
{
	char *head = NULL;
	char header_val[256];
	response_t *resp = NULL;
	int statcode;
	long content_length;
	int fd = -1;
	int ret;
	
	int count;

	char *url = NULL;
	char *referer = NULL;
	char *content_buf = NULL;

	url_t *u = NULL;

	int depth;
	int parse_error;
	
	pthread_t self;
	int thread_id;
	
	int exit = 0;

	self = pthread_self();	
	thread_id = get_thread_id(pool, self);

	ret = url_dequeue(queue, &url, &referer, &depth);

	if (url == NULL)
	{
		pthread_mutex_lock(&flag.f_lock);
		SETBIT(flag.queue_empty, thread_id);
		pthread_mutex_unlock(&flag.f_lock);

		exit = check_exit();
						
		goto cleanup;
	}

	pthread_mutex_lock(&flag.f_lock);
	CLRBIT(flag.queue_empty, thread_id);
	pthread_mutex_unlock(&flag.f_lock);

	count = url_get_queue_count(queue);

	printf("From URL: %s, remains: %d\n", url, count);	

	u = url_parse(url, &parse_error);

	if (!u)
	{
		printf("%s\n", url_error(parse_error));
		goto cleanup;
	}
		

	ret = establish_connection(&fd, u->host, u->port);

	if (ret < 0) 
		goto cleanup;	

	ret = send_request(fd, u);
	head = read_http_resp_head(fd);
	
	resp = resp_new(head);
	statcode = resp_status(resp);
	printf("status code: %d\n", statcode);
	
	if (statcode != 200)
		goto cleanup;	

	if (resp_header_copy(resp, "Content-Length", header_val, 
			sizeof(header_val)))
	{
		long parsed;

		parsed = strtoll(header_val, NULL, 10);
	
		if (parsed < 0)
		{
			content_length = -1;
		}
		else
			content_length = parsed;
	}
	
	if (content_length < 0)
		goto cleanup;
	
	content_buf = (char *)calloc(content_length + 1, 1);

	if (content_buf == NULL)
	{
		perror("Failed to allocate content buffer!");
		goto cleanup;
	}

	read_resp_body(fd, content_length, content_buf);
		
	{
		struct url_vec *vec_head = NULL;
		struct url_vec *vec_tail = NULL;
		char *url_merged         = NULL;	
		url_t *url_parsed        = NULL;

		vec_head = extract_urls(content_buf);
		vec_tail = vec_head;

		for (; vec_tail; vec_tail = vec_tail->next)
		{
			url_merged = uri_merge(url, vec_tail->url);
	
			url_simplify(url_merged);
	
			if (url_sanity_check(url_merged))
			{
				if (webgraph_contains(graph, url_merged))
				{
					webgraph_add_link(graph, url_merged, url);

					free(url_merged);
					continue;
				}

				webgraph_add_url(graph, url_merged);	
				webgraph_add_link(graph, url_merged, url);
				url_enqueue(queue, url_merged, NULL, NULL); 
			}
			else
			{
				free(url_merged);
			}
		}
		free_url_vec(vec_head);	
	}
	free(content_buf);	
cleanup:
	if (fd > 0)
		close(fd);
	
	if (head)
		free(head);
	if (u)	
		url_free(u);
	if (resp)
		resp_free(resp);
	if (url)
		free(url);
	if (referer)
		free(referer);
	if (exit)
		pthread_exit(NULL);
}


int main(int argc, char *argv[])
{
	const char *seed_url = "http://10.108.106.36/pcourse/index.html";
	int depth = 1;

	char *url = NULL;
	char *referer = NULL;

	/* Create thread pool */
	pool = create_threadpool(NUM_THREADS);

	/* Create url queue */
	queue = url_queue_new();
	
	if (queue == NULL)
	{
		fprintf(stderr, "Failed to create url queue!\n");
		return 1;
	}

	/* Create web graph */
	graph = webgraph_new(500000);
	
	if (graph == NULL)
	{
		fprintf(stderr, "Failed to create web graph!\n");
		return 1;
	}

	webgraph_add_url(graph, seed_url);	
	url_enqueue(queue, strdup(seed_url), NULL, depth);

	/* Dispatch web crawling job */
	while(get_num_thread_alive(pool) > 0)
	{
		dispatch(pool, retrieve_webpage, NULL);	
		usleep(10000);
	}

	destroy_threadpool(pool); 
	
	pagerank(graph, 0.85, 0.0000001);  
	print_top_n(graph, 10);

	/* Clean up */
	webgraph_delete(graph);
	url_queue_delete(queue);

	return 0;
}
