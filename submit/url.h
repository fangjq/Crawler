#ifndef _URL_H
#define _URL_H
#include <pthread.h>

typedef struct url
{
	char *url;

	char *host;
	int port;

	char *path;
}url_t;

struct url_vec
{
	char *url;
	struct url_vec *next;
};

struct queue_element
{
	const char *url;
	const char *referer;
	int depth;

	struct queue_element *next;
};

struct url_queue
{
	struct queue_element *head;
	struct queue_element *tail;
	int count, maxcount;
	pthread_mutex_t qlock;
};

extern int url_simplify(char *url);

extern char *uri_merge(const char *base, const char *link);

extern struct url_queue *url_queue_new(void);

extern void url_queue_delete(struct url_queue *queue);

extern void url_enqueue(struct url_queue *queue,
				   const char *url,
				   const char *referer,
				   int depth);

extern int url_dequeue(struct url_queue *queue,
				 	   char **url,
					   char **referer,
					   int *depth);



extern const char *url_error(int error_code);

extern int url_get_queue_count(struct url_queue *queue);

extern url_t *url_parse(const char *url, int *error); 

#endif
