#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <regex.h>
#include "http.h"
#include "utils.h"


#define SOCK_TIMEOUT 20

#define HTTP_RESPONSE_MAX_SIZE 65536

#define HTTP_DEFAULT_PORT 80


static int resp_header_locate(const response_t *resp, 
		const char *name, int start, 
		const char **begptr, const char **endptr)
{
	int i;
	const char **headers = resp->headers;
	int name_len;

	if (!headers || !headers[1])
		return -1;
	
	name_len = strlen(name);
	if (start > 0)
		i = start;
	else 
		i = 1;

	for (; headers[i + 1]; i++)
	{
		const char *b = headers[i];
		const char *e = headers[i + 1];
		if (e - b > name_len
			&& b[name_len] == ':'
					&& 0 == strncasecmp(b, name, name_len))
		{
			b += name_len + 1;
			while (b < e && isspace(*b))
				++b;
			while (b < e && isspace(e[-1]))
				--e;
			*begptr = b;
			*endptr = e;
			return i;
		}
	}
	return -1;
}


static int resp_header_get(const response_t *resp, const char *name,
		const char **begptr, const char **endptr)
{
	int pos = resp_header_locate(resp, name, 0, begptr, endptr);
	return pos != -1;
}

int resp_header_copy(const response_t *resp, 
		const char *name, char *buf, int bufsize)
{
	const char *b, *e;
	if (!resp_header_get(resp, name, &b, &e))
		return 0;
	if (bufsize)
	{
		int len = MIN(e - b, bufsize - 1);
		memcpy(buf, b, len);
		buf[len] = '\0';
	}
	return 1;
}

int resp_status(const response_t *resp)
{
	int status;
	const char *p, *end;

	if (!resp->headers)
	{
		return 200;
	}
	
	p = resp->headers[0];
	end = resp->headers[1];
	
	if (!end)
		return -1;

	if (end - p < 4 || 0 != strncmp(p, "HTTP", 4))
		return -1;
	p += 4;
	
	if (p < end && *p == '/')
	{
		++p;
		while (p < end && isdigit(*p))
			++p;
		if (p < end && *p == '.')
			++p;
		while (p < end && isdigit(*p))
			++p;
	}

	while (p < end && isspace(*p))
		++p;
	if (end - p < 3 || !isdigit(p[0]) || !isdigit(p[1]) ||
			!isdigit(p[2]))
		return -1;
	
	status = 100 * (p[0] - '0') + 10 * (p[1] - '0') + (p[2] - '0');

	return status;		 	
}

response_t *resp_new(const char *head)
{
	const char *hdr;
	int count, size;
	
	response_t *resp = calloc(1, sizeof(response_t));
	resp->data = head;
	
	if (*head == '\0')
	{
		return resp;
	}

	size = count = 0;
	hdr = head;
	while (1)
	{
		DO_REALLOC(resp->headers, size, count + 1, const char *);
		resp->headers[count++] = hdr;
		
		if (!hdr[0] || (hdr[0] == '\r' && hdr[1] == '\n') ||
				hdr[0] == '\n')
			break;
		do
		{
			const char *end = strchr(hdr, '\n');
			if (end)
				hdr = end + 1;
			else
				hdr += strlen(hdr);
		}
		while (*hdr == ' ' || *hdr == '\t');			 
	}
	DO_REALLOC(resp->headers, size, count + 1, const char *);
	resp->headers[count] = NULL;
	return resp;
}

void resp_free(response_t *resp)
{
	free(resp->headers);
	free(resp);
}

static int sock_peek(int fd, char *buf, int bufsize, double timeout)
{
	int res;
	do
		res = recv(fd, buf, bufsize, MSG_PEEK);
	while (res == -1 && errno == EINTR);
	return res;
}

static int sock_read(int fd, char *buf, int bufsize, double timeout)
{
	int res;
	do 
		res = read(fd, buf, bufsize);
	while (res == -1 && errno == EINTR);
	return res;
}

static const char *http_resp_header_terminator(
		const char *start, const char *peeked, int peeklen)
{
	const char *p, *end;

	if (start == peeked && 0 != memcmp(
			start, "HTTP", MIN(peeklen, 4)))
		return start;

	p = peeked - start < 2 ? start : peeked - 2;
	end = peeked + peeklen;

	for (; p < end - 2; p++)
		if (*p == '\n')
		{
			if (p[1] == '\r' && p[2] == '\n')
				return p + 3;
			else if (p[1] == '\n')
				return p + 2;
		}

	if (p[0] == '\n' && p[1] == '\n')
		return p + 2;
	
	return NULL;	
} 

char *read_http_resp_head(int fd)
{
	long bufsize = 512;
	char *hunk = malloc(bufsize);
	int tail = 0;

	while (1)
	{
		const char *end;
		int pklen, rdlen, remain;
		pklen = sock_peek(
			fd, hunk + tail, bufsize - 1 - tail, SOCK_TIMEOUT);

		if (pklen < 0)
		{
			free(hunk);
			return NULL;
		}
		end = http_resp_header_terminator(
			hunk, hunk + tail, pklen);
		if (end)
		{
			remain = end - (hunk + tail);
			if (remain == 0)
			{
				hunk[tail] = '\0';
				return hunk;
			}
			if (bufsize - 1 < tail + remain)
			{
				bufsize = tail + remain + 1;
				hunk = realloc(hunk, bufsize);
			}
		}
		else
			remain = pklen;

		rdlen = sock_read(fd, hunk + tail, remain, SOCK_TIMEOUT);
		if (rdlen < 0)
		{
			free(hunk);
			return NULL;
		}
		tail += rdlen;
		hunk[tail] = '\0';
			
		if (rdlen == 0)
		{
			if (tail == 0)
			{
				free(hunk);
				errno = 0;
				return NULL;
			}
			else
				return hunk;
		}
			
		if (end && rdlen == remain)
			return hunk;

		if (tail == bufsize - 1)
		{
			if (bufsize >= HTTP_RESPONSE_MAX_SIZE)
			{
				free(hunk);
				errno = ENOMEM;
				return NULL;
			}
			bufsize <<= 1;
			if (bufsize > HTTP_RESPONSE_MAX_SIZE)
				bufsize = HTTP_RESPONSE_MAX_SIZE;
			hunk = realloc(hunk, bufsize);
		}	
	}
}

int establish_connection(int *fd, char *url, int port)
{
	int sock;
	struct addrinfo hints, *result;
	int ret;
	
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if ((ret = getaddrinfo(url, "http", &hints, &result)))
	{
		return -1;
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror("Can't create TCP socket!");
		freeaddrinfo(result);
		return -1;
	}

	if (connect(sock, result->ai_addr, result->ai_addrlen) < 0)
	{
		perror("Could not connect");
		freeaddrinfo(result);
		return -1;
	}
	
	*fd = sock;
	freeaddrinfo(result);	
	return 0;
}

/*

int establish_connection(int *fd, char *url, int port)
{
	int sock;
	struct sockaddr_in addr;
	struct hostent *hent;
	
	if ((hent = gethostbyname(url)) == NULL)
	{
		perror("Can't get host ip!");
		return -1;
	}

	if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror("Can't create TCP socket!");
		return -1;
	}


	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	memcpy(&addr.sin_addr, hent->h_addr_list[0], hent->h_length);

	if (connect(sock, &addr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("Could not connect");
		return -1;
	}

	*fd = sock;
	return 0;
}

*/

int send_request(int fd, url_t *u)
{
	int req_len, sent;
	int ret;
	char request[1024];

	sprintf(request, 
		"GET /%s HTTP/1.0\r\n"
		"HOST: %s\r\n"
		"Accept: */*\r\n"
		"Connection: Keep-Alive\r\n"
		"User-Agent: Mozilla/5.0 (compatible; spiderchan/1.0;)"
		"\r\n"
		"Referer: %s\r\n\r\n", u->path, u->host, u->host);
	
	req_len = strlen(request);
	sent = 0;
	while (sent < req_len)
	{
		ret = send(fd, request + sent, req_len - sent, 0);
		if (ret == -1)
		{
			perror("Can't send query");
			return -1;
		}
		sent += ret;
	}
	return 0;
}

int read_resp_body(int fd, long toread, char *buf)
{
	long sum_read = 0;
	int ret;

	while (sum_read < toread)
	{
		int rdsize;
		ret = sock_read(fd, buf + sum_read, toread - sum_read, SOCK_TIMEOUT);
		
		if (ret <= 0)
			break;

		sum_read += ret;
	}
	
	if (ret < -1)
		ret = -1;

	return ret;	
}


int get_urls()
{
	char *head;
	char header_val[256];
	response_t *resp;
	int statcode;
	int content_length;
	int fd;
	int ret;
	char *content_buf;
	
	const char *url = "http://www.baidu.com";

	
	url_t *u;

	u = url_parse(url, NULL);
	ret = establish_connection(&fd, u->host, u->port);

	send_request(fd, u);

	head = read_http_resp_head(fd);

	printf("%s\n", head);

	resp = resp_new(head);
	statcode = resp_status(resp);
	
	if (resp_header_copy(resp, "Content-Length", header_val,
			sizeof(header_val)))
	{
		long parsed;
		parsed = strtoll(header_val, NULL, 10);
		if (errno == ERANGE)
		{
			content_length = -1;
		}
		else if (parsed < 0)
		{
			content_length = -1;
		}
		else
			content_length = parsed;
	}
	
	content_buf = (char *) malloc(content_length);	
	read_resp_body(fd, content_length, content_buf);
	{
		struct url_vec *child_urls;
		struct url_vec *child_pos;
		child_urls = extract_urls(content_buf);
		char *url_merged;

		child_pos = child_urls->next;

		for (; child_pos; child_pos = child_pos->next)
		{
			url_merged = uri_merge(url, child_pos->url);
			printf("Before: %s\nAfter: %s\n\n", child_pos->url,
					url_merged);
			
			free(url_merged);
		}

		free_url_vec(child_urls);
	}

	free(content_buf);
	url_free(u);
	resp_free(resp);
}

