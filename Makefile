TEAM = NOBODY
VERSION = 1
HANDINDIR = /afs/cs/academic/class/15213-f02/L7/handin

CC = gcc
CFLAGS = -Wall -O0 -g
#LDFLAGS = -lpthread
FILES = client server

all: $(FILES)

server: rio.o

rio.o: rio.h rio.c


handin:
	cp proxy.c $(HANDINDIR)/$(TEAM)-$(VERSION)-proxy.c

clean:
	rm -f ./*.o ./client ./server

