TEAM = NOBODY
VERSION = 1
HANDINDIR = /afs/cs/academic/class/15213-f02/L7/handin

CC = gcc
CFLAGS = -Wall -O0 -g
LDFLAGS = -lpthread
FILES = proxy

all: $(FILES)

proxy: csapp.o

csapp.o: csapp.h csapp.c


handin:
	cp proxy.c $(HANDINDIR)/$(TEAM)-$(VERSION)-proxy.c

clean:
	rm -f ./*.o ./proxy
# Push sources to my Linux development machine
PUSH_CONTENTS=proxy.c csapp.h csapp.c
PUSH_DEST=siyuan@centos:projects/proxy_lab
push:
	rsync -auv --filter=':- .gitignore' $(PUSH_CONTENTS) $(PUSH_DEST)
