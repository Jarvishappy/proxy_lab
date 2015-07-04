/**
 * Echo client
 *
 * connect to echo server,
 * write a message to network,
 * receive message from server,
 * print the message to stdout
 *
 */
#include "netlib.h"

#define SERV_HOST   "127.0.0.1"
#define SERV_PORT   8888
#define BUFSIZE     256

int read_from(int fd, char *buf, const int size);
void write_to(int fd, char *buf, const int size);

int main(int argc, char *argv[])
{
    int fd;
    if (argc < 3) {
        printf("Usage: client <host> <port>\n");
        exit(1);
    }

    fd = open_clientfd(argv[1], atoi(argv[2]));

    /* 2. read message from stdin */
    char *linep = NULL;
    size_t len = BUFSIZE;
    char readbuf[BUFSIZE] = {0};

    /**
     * 读socket的时候，read()返回0表示socket关闭了(EOF, no more data to be read)
     * 写入一个closed socket，会报什么错？write() return -1 and set errno to EBADF
     *
     */
    while (getline(&linep, &len, stdin) != -1) {
        /* 3. write message to server */
        len = strlen(linep);
        write_to(fd, linep, len);
        read_from(fd, readbuf, len);
        printf("received: %s", linep);
    }
    if (close(fd))
        unix_error("close");


    return 0;
}


/**
 * read from fd until encounter an EOF
 * @return num of read bytes
 */
int read_from(int fd, char *buf, const int size)
{
    int nleft = size;
    int nread;
    char *bufp = buf;
    while (nleft > 0) {
        nread = read(fd, bufp, nleft);
        if (nread == -1) {
            if (errno == EINTR)
                nread = 0;
            else
                return -1;
        } else if (nread == 0) /* EOF, socket has been closed */
            break;

        nleft -= nread;
        bufp += nread;

    }

    return size - nleft;
}

/**
 * write all byte in buf to fd
 */
void write_to(int fd, char *buf, const int size)
{
    ssize_t nwritten;
    ssize_t nleft = size;
    char *bufp = buf;

    while (nleft > 0) {
        nwritten = write(fd, bufp, nleft);
        if (nwritten < 0) {
            if (errno == EINTR)
                nwritten = 0;
            else
                unix_error("write");
        }

        nleft -= nwritten;
        bufp += nwritten;
    }
}



