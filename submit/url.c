#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <regex.h>

#include "url.h"
#include "utils.h"

#define HTTP_DEFAULT_PORT 80

static const char *const HTTP_SCHEME = "http://";

#define URL_HAS_SCHEME(url) \
	(0 == strncasecmp(url, HTTP_SCHEME, strlen(HTTP_SCHEME)))

#define URL_SKIP_SCHEME(url) do{\
	*(url) += strlen(HTTP_SCHEME);\
}while(0);

static const char *path_end(const char *url)
{
	const char *seps = "?#";
	return strpbrk_or_eos(url, seps);
}

#define find_last_char(b, e, c) memrchr((b), (c), (e) - (b))

int url_simplify(char *url)
{
	char *h = url;
	char *t = url;
	char *beg = url;
	char *end = strchr(url, '\0');

	while (h < end)
	{
		if (h[0] == '.' && (h[1] == '/' || h[1] == '\0'))
		{
			h += 2;
		}
		else if (h[0] == '.' && h[1] == '.' &&
			(h[2] == '/' || h[2] == '\0'))
		{
			if (t > beg)
			{
				for (--t; t > beg && t[-1] != '/'; t--)
					;
			}
			h += 3;
		}
		else
		{
			if (t == h)
			{
				while (h < end && *h != '/')
					t++, h++;
				if (h < end)
					t++, h++;
			}
			else
			{
				while (h < end && *h != '/')
					*t++ = *h++;
				if (h < end)
					*t++ = *h++;
			}
		}	
	}
	if (t != h)
		*t = '\0';

	return t != h;	
}

char *uri_merge(const char *base, const char *link)
{
	int linklength;
	const char *end;
	char *merge = NULL;

	if (URL_HAS_SCHEME(link))
		return strdup(link);

	end = path_end(base);
	linklength = strlen(link);

	if (!*link)
	{
		return strdup(base);
	}
	else if (*link == '?')
	{
		int baselength = end - base;
		merge = (char *) malloc(baselength + linklength + 1);
		memcpy(merge, base, baselength);
		memcpy(merge + baselength, link, linklength);
		merge[baselength + linklength] = '\0';
	}
	else if (*link == '#')
	{
		int baselength;
		const char *end1 = strchr(base, '#');
		if (!end1)
			end1 = base + strlen(base);
		baselength = end1 - base;
		merge = (char *) malloc(baselength + linklength + 1);
		memcpy(merge, base, baselength);
		memcpy(merge + baselength, link, linklength);
		merge[baselength + linklength] = '\0';
	}
	else if (*link == '/' && *(link + 1) == '/')
	{
		int span;
		const char *slash;
		const char *start_insert;

		slash = memchr(base, '/', end-base);
		
		if (slash && *(slash + 1) == '/')
			start_insert = slash;
		else
			start_insert = base;

		span = start_insert - base;
		merge = (char *) malloc(span + linklength + 1);
		if (span)
		 	memcpy(merge, base, span);
		memcpy(merge + span, link, linklength);
		merge[span + linklength] = '\0';
	}
	else if (*link == '/')
	{
		int span;
		const char *slash;
		const char *start_insert = NULL;
		const char *pos = base;
		int seen_slash_slash = 0;

		while(1)
		{
			slash = memchr(pos, '/', end - pos);
			if (slash && !seen_slash_slash &&
					*(slash + 1) == '/')
			{
				pos = slash + 2;
				seen_slash_slash = 1;
			}
			else
				break;
		}		

		if (!slash && !seen_slash_slash)
			start_insert = base;			
		else if (!slash && seen_slash_slash)
			start_insert = end;
		else if (slash && !seen_slash_slash)
			start_insert = base;
		else if (slash && seen_slash_slash)
			start_insert = slash;

		span = start_insert - base;
		merge = (char *) malloc(span + linklength + 1);
		if (span)
			memcpy(merge, base, span);
		memcpy(merge + span, link, linklength);
		merge[span + linklength] = '\0';
	}
	else
	{
		int need_explicit_slash = 0;
		int span;
		const char *start_insert;
		const char *last_slash = find_last_char(base, end, '/');
		if (!last_slash)
		{
			start_insert = base;
		}
		else if (last_slash && last_slash > base + 2 &&
				last_slash[-2] == ':' && last_slash[-1] == '/')
		{
			start_insert = end + 1;
			need_explicit_slash = 1;
		}
		else
		{
			start_insert = last_slash + 1;
		}
		
		span = start_insert - base;
		merge = (char *) malloc(span + linklength + 1);
		if (span)
			memcpy(merge, base, span);
		if (need_explicit_slash)
			merge[span - 1] = '/';
		memcpy(merge + span, link, linklength);
		merge[span + linklength] = '\0';
	}

	return merge;
}





struct url_queue *url_queue_new(void)
{
	struct url_queue *queue = (struct url_queue *)
		calloc(1, sizeof(struct url_queue));
	
	if (pthread_mutex_init(&queue->qlock, NULL) != 0)
	{
		free(queue);
		return NULL;
	}
	
	return queue;
}

void url_queue_delete(struct url_queue *queue)
{
	pthread_mutex_destroy(&queue->qlock);	
	free(queue);
}

void url_enqueue(struct url_queue *queue, 
						const char *url,
						const char *referer,
						int depth)
{
	struct queue_element *qel = (struct queue_element *)
		malloc(sizeof(struct queue_element));
	qel->url = url;
	qel->referer = referer;
	qel->depth = depth;
	qel->next = NULL;
	
	pthread_mutex_lock(&queue->qlock);

	++queue->count;
	if (queue->count > queue->maxcount)
		queue->maxcount = queue->count;

	if (queue->tail)
		queue->tail->next = qel;
	queue->tail = qel;
	
	if (!queue->head)
		queue->head = queue->tail;

	pthread_mutex_unlock(&queue->qlock);
}

int url_dequeue(struct url_queue *queue,
					   char **url,
					   char **referer,
					   int *depth)
{
	struct queue_element *qel;

	pthread_mutex_lock(&queue->qlock);

	qel = queue->head;
	if (!qel)
	{
		pthread_mutex_unlock(&queue->qlock);
		return 0;
	}

	queue->head = queue->head->next;
	if (!queue->head)
		queue->tail = NULL;

	*url = qel->url;
	*referer = qel->referer;
	*depth = qel->depth;
		
	--queue->count;

	free(qel);
	
	pthread_mutex_unlock(&queue->qlock);

	return 1;
}

int url_get_queue_count(struct url_queue *queue)
{
	int ret;
	pthread_mutex_lock(&queue->qlock);
	ret = queue->count;
	pthread_mutex_unlock(&queue->qlock);

	return ret;
}

static const char *parse_errors[] = {
#define URL_NO_ERROR  					0
"No error",
#define URL_MISSING_SCHEME              1
"Scheme missing",
#define URL_INVALID_HOST_NAME           2
"Invalid host name",
#define URL_BAD_PORT_NUMBER             3
"Bad port number"
};


url_t *url_parse(const char *url, int *error)
{
	url_t *u;
	const char *p;
	
	int port;
	int error_code;

	const char *host_b, *host_e;
	
	const char *path_b, *path_e;
	
	const char *seps = ":/";

	p = url;

	if (!URL_HAS_SCHEME(url))
	{
		error_code = URL_MISSING_SCHEME;
		goto error;
	}

	URL_SKIP_SCHEME(&p);
	
	path_b = path_e = NULL;

	host_b = p;

	p = strpbrk_or_eos(p, seps);	
	host_e = p;

	++seps;

	if (host_b == host_e)
	{
		error_code = URL_INVALID_HOST_NAME; 
		goto error;		
	}
	
	port = HTTP_DEFAULT_PORT;
	if (*p == ':')
	{		
		const char *port_b, *port_e, *pp;
		++p;
		port_b = p;
		p = strpbrk_or_eos(p, seps);
		port_e = p;
	
		if (port_b != port_e)
		{
			for (port = 0, pp = port_b; pp < port_e; pp++)
			{
				if (!isdigit(*pp))
				{
					error_code = URL_BAD_PORT_NUMBER;
					goto error;
				}
				port = 10 * port + (*pp - '0');
				if (port > 0xffff)
				{
					error_code = URL_BAD_PORT_NUMBER;
					goto error;
				}
			}
		}
	}
	++seps;
	
	if (*p == '/')
		path_b = ++p, path_e = p = strpbrk_or_eos(p, seps);

	u = (url_t *) malloc(sizeof(url_t));
	u->host = strdupdelim(host_b, host_e);
	u->port = port;
	
	u->path = strdupdelim(path_b, path_e);
	u->url = strdup(url);
	return u;

error:
	if (error)
		*error = error_code;
	return NULL;	
}

const char *url_error(int error_code)
{
	return parse_errors[error_code];
}

void url_free(url_t *url)
{
	free(url->host);
	free(url->path);
	free(url->url);

	free(url);
}

char *rewrite_shorthand_url(const char *url)
{
	int size = 32;
	int n;
	const char *p;
	char *ret;

	if (URL_HAS_SCHEME(url))
		return NULL;

	p = strpbrk(url, ":/");
	if (p = url)
		return NULL;
	
	if (p && p[0] == ':' && p[1] == '/' && p[2] == '/')
		return NULL;
	
	
	ret = (char *) malloc(size);
	while (1)
	{
		n = snprintf(ret, size, "http://%s", url);
		
		if (n > -1 && n < size)
			return ret;
		
		if (n > -1)
			size = n + 1;
		
		ret = realloc(ret, size);	
	}			
}

int url_sanity_check(const char *url)
{
	const char *accept_pattern = 
		"^http\\://10\\.108\\.106\\.36.*\\.(htm|html)$";
	
	regex_t re;
	int ret;	

	if (regcomp(&re, accept_pattern, REG_EXTENDED) != 0)
	{
		fprintf(stderr, "Regex compile error!\n");
		exit(1);
	}

	if (regexec(&re, url, 0, NULL, 0) == REG_NOMATCH)
		ret = 0;
	else
		ret = 1;
	
	regfree(&re);
	return ret;	
}

struct url_vec *extract_urls(const char *content)
{
	const char *href_pattern = "href=\"\\s*\\([^ >\"]*\\)\\s*\"";
	regex_t re;
	const size_t nmatch = 2;
	regmatch_t matchptr[nmatch];
	int len;
	char *p = content;
	struct url_vec *head;
	struct url_vec *tail;

	if (regcomp(&re, href_pattern, 0) != 0)
	{
		exit(1);
	}

	head = tail = NULL;
	
	while (regexec(&re, p, nmatch, matchptr, 0) != REG_NOMATCH)
	{
		struct url_vec *entry;

		len = (matchptr[1].rm_eo - matchptr[1].rm_so);
		p = p + matchptr[1].rm_so;
		char *tmp = (char *)calloc(len + 1, 1);
		strncpy(tmp, p, len);
		tmp[len] = '\0';

		entry = (struct url_pos *)calloc(1, sizeof(struct url_vec));
		entry->url = tmp;
		
		if (head == NULL)
			head = entry;
		else
			tail->next = entry;
		tail = entry;
		
		p = p + len + (matchptr[0].rm_eo - matchptr[1].rm_eo);
	}

	regfree(&re);

	return head;
}

void free_url_vec(struct url_vec *l)
{
	while(l)
	{
		struct url_vec *next = l->next;
		if (l->url)
			free(l->url);
		free(l);
		l = next;
	}
}	
