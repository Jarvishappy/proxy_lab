#ifndef RIO_H
#define RIO_H
#include <sys//types.h>

ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
ssize_t rio_readlinen(int fd, void *usrbuf, size_t n);


#endif
