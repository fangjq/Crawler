TARGET     = site_analyzer
CC         = cc
DEFINES    = 
GDB        = -g -rdynamic
CFLAGS     = -Wall -pedantic $(DEFINES) $(GDB)
INCPATH    = 
LINK       = cc
LIBS       = -lpthread
LFLAGS     = 

DEL_FILE   = rm -f 

SOURCES = main.c \
		  threadpool.c \
		  http.c \
		  url.c \
		  utils.c \
		  hash.c \
		  webgraph.c 

OBJECTS = main.o \
		  threadpool.o \
		  http.o \
		  url.o  \
		  utils.o \
		  hash.o \
		  webgraph.o


all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(LINK) -o $(TARGET) $(LFLAGS) $(LIBS) $(OBJECTS)

threadpool.o: threadpool.c threadpool.h
	$(CC)  $(CFLAGS) $(INCPATH) -o threadpool.o -c threadpool.c

main.o: main.c
	$(CC) $(CFLAGS) $(INCPATH) -o main.o -c main.c

http.o: http.c 
	$(CC) $(CFLAGS) $(INCPATH) -o http.o -c http.c

url.o: url.c
	$(CC) $(CFLAGS) $(INCPATH) -o url.o -c url.c

utils.o: utils.c
	$(CC) $(CFLAGS) $(INCPATH) -o utils.o -c utils.c

hash.o: hash.c
	$(CC) $(CFLAGS) $(INCPATH) -o hash.o -c hash.c

webgraph.o: webgraph.c
	$(CC) $(CFLAGS) $(INCPATH) -o webgraph.o -c webgraph.c

clean:
	-$(DEL_FILE) $(OBJECTS)
