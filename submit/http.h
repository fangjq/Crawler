#ifndef _HTTP_H
#define _HTTP_H
#include "url.h"

typedef struct http_header
{
	char *content_type;
	int status_code;
}http_header_t;

typedef struct http_response
{
	http_header_t *header;
	char *body;
	int body_len;
}http_response_t;

typedef struct response{
	const char *data;

	const char **headers;
}response_t;


extern int establish_connection();

extern int send_request(int fd, url_t *u);

extern int read_resp_body(int fd, long toread, char *buf);
#endif
