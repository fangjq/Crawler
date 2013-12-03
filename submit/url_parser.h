#ifndef _HTML_PARSER_H
#define _HTML_PARSER_H

typedef struct url
{
	char *domain;
	char *path;
	int port;
	char *ip;
	int level;
}url_t;

push_url_queue();

pop_url_queue();

parse_url()


#endif
