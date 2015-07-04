#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

/**
 * Read n bytes from file fd to buffer pointed to by usrbuf
 * @return number of bytes read
 */
ssize_t rio_readn(int fd, void *usrbuf, size_t n)
{
    ssize_t nread = 0;
    size_t nleft = n;
    char *bufp = usrbuf; /* void* pointer cannot do pointer arithmetic! */

    while (nleft > 0) {
        if ((nread = read(fd, bufp, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return -1;
        }
        else if (nread == 0) /* EOF */
            break;

        nleft -= nread;
        bufp += nread;
    }

    return n - nleft;
}

/**
 * Write n bytes in buffer pointed by usrbuf to file fd
 * @return number of bytes written
 */
ssize_t rio_writen(int fd, void *usrbuf, size_t n)
{
    size_t nleft = n;
    ssize_t nwritten = 0;
    char *bufp = usrbuf; /* void* pointer cannot do pointer arithmetic! */

    while (nleft > 0) {
        if ((nwritten = write(fd, bufp, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else    /* error occured */
                return -1;
        }
        nleft -= nwritten;
        bufp += nwritten;
    }

    return n;
}

/**
 * Read byte one by one from fd, until encounter a line feed.
 * @return -1 for error, num of byte read
 */
ssize_t rio_readlinen(int fd, void *usrbuf, size_t maxlen)
{
    ssize_t nread = 0;
    char *bufp = usrbuf; /* void* pointer cannot do pointer arithmetic! */

    size_t n;
    char c;
    for (n = 1; n < maxlen; n++) {
       if ((nread = read(fd, &c, 1)) < 0) {
           if (errno == EINTR)
               continue;
           else
               return -1;
       } else if (nread == 0) { /* EOF */
           if (n == 1)  /* 没读到任何数据 */
               return 0;
           else         /* 读到一部分数据 */
               break;
       }

       *bufp++ = c;

       if (c == '\n')
           break;
    }

    *bufp = '\0'; /* 不要漏了这个啊！ */

    return n;
}



