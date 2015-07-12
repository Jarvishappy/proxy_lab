/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Andrew Carnegie, ac00@cs.cmu.edu
 *     Harry Q. Bovik, bovik@cs.cmu.edu
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */

#include <execinfo.h>
#include <signal.h>
#include "csapp.h"
#define DBG(x) do { printf("%s:%d - ", __func__, __LINE__); printf x; \
    putchar('\n'); fflush(stdout); } while(0);

#define MAXHEAD 30
#define CRLF "\r\n"
#define CONTENT_LENGTH "Content-Length"
#define HOST "Host"

/**
 * Represents a HTTP request
 * TODO: 没必要把整个HTTP message全部读到内存里，尤其是header
 * 读完request line之后就有足够的参数connect到target server了，
 * 然后就可以读一行写一行，这样能节省很多内存
 */
typedef struct {
    int content_len;
    int port;
    char method[5];
    char host[128];
    char path[1024];
    char uri[MAXLINE];
} reqmeta_t;

typedef struct {
    char linebuf[MAXLINE];
    char bodybuf[MAXBUF];
} io_buf_t;

void handler(int sig) {
    void *array[10];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 10);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    exit(1);
}

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void write_log(FILE *fp, char *logstring);

/**
 * 解析HTTP Request的第一行，获取目的server的host和port
 */
int parse_request_line(rio_t *rio_buf, reqmeta_t *reqmeta, io_buf_t *io_buf)
{
    if (Rio_readlineb(rio_buf, io_buf->linebuf, MAXLINE) == 0)
        return 0;

    /* check if a valid request line */
    if (strstr(io_buf->linebuf, "HTTP") == NULL)
        return -1;

    char skipbuf[64];
    sscanf(io_buf->linebuf, "%s %s %s", reqmeta->method, reqmeta->uri, skipbuf);

/*     char *p = io_buf->linebuf;
 *     char *delim;
 * 
 *     delim = strchr(p, ' ');
 *     if (!delim)
 *         return -1;
 * 
 *     *delim = '\0';
 *     [> p points to the method <]
 *     strncpy(reqmeta->method, p, strlen(p) + 1);
 * 
 *     p = delim + 1;
 *     delim = strchr(p, ' ');
 *     if (!delim)
 *         return -1;
 * 
 *     *delim = '\0';
 *     [> p points to the uri <]
 *     strncpy(reqmeta->uri, p, strlen(p) + 1); */
    parse_uri(reqmeta->uri, reqmeta->host, reqmeta->path, &reqmeta->port);

    return 1;
}

int get_header(rio_t *rio_buf, reqmeta_t *reqmeta, io_buf_t *io_buf);
int get_body(rio_t *rio_buf, io_buf_t *io_buf, int content_len);

ssize_t send_headers(int fd, rio_t *rio_buf, io_buf_t *io_buf, reqmeta_t *reqmeta);
int send_body(int fd, rio_t *rio_buf, io_buf_t *io_buf, int content_len);

void clear_reqmeta(reqmeta_t *reqmeta)
{
    memset(reqmeta, 0, sizeof(*reqmeta));
}

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    signal(SIGSEGV, handler);
    /* ignore SIGPIPE signaled when write to closed socket */
    signal(SIGPIPE, SIG_IGN);

    int port;
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    port = atoi(argv[1]);
    int listenfd, browserfd, targetfd;
    struct sockaddr_in browser_addr;
    socklen_t addrlen = sizeof(browser_addr);

    /* open listenning fd */
    listenfd = Open_listenfd(port);

    FILE *flogp = fopen("proxy.log", "a");
    if (!flogp)
        unix_error("fopen");

    printf("server: started on port: %d.\n", port);

    /* we only need two buffer */
    rio_t browser_buf;
    rio_t target_buf;
    io_buf_t io_buf;
    char logstring[MAXLINE];
    ssize_t nbyte;
    int rc;

    reqmeta_t reqmeta;
    memset(&reqmeta, 0, sizeof(reqmeta));
    while (1) {
        browserfd = Accept(listenfd, (struct sockaddr *)&browser_addr, &addrlen);
        DBG(("accept a new connection"));

        Rio_readinitb(&browser_buf, browserfd);

        while (1) {
            /* parse request line */
            rc = parse_request_line(&browser_buf, &reqmeta, &io_buf);
            if (rc == 0) {
                DBG(("browser socket has been closed"));
                break;
            }
            if (rc == -1)
                continue;

            /* connect to target server */
            DBG(("host: %s, port: %d", reqmeta.host, reqmeta.port));
            targetfd = Open_clientfd(reqmeta.host, reqmeta.port);
            /* TODO: set the accepted socket to non-blocking  */


            /* send HTTP request to target server */
            /* request line */
            sprintf(io_buf.linebuf, "%s %s HTTP/1.1\r\n", reqmeta.method, reqmeta.path);
            DBG(("sent request line: %s, length: %ld", io_buf.linebuf, strlen(io_buf.linebuf)));
            Rio_writen(targetfd, io_buf.linebuf, strlen(io_buf.linebuf));
            reqmeta.content_len = 0;

            /* headers */
            if (send_headers(targetfd, &browser_buf, &io_buf, &reqmeta) == -1 ||
                send_body(targetfd, &browser_buf, &io_buf, reqmeta.content_len) == -1)
                break;

            DBG(("forwarded request to target server"));

            /**** send response back to browser *****/
            Rio_readinitb(&target_buf, targetfd);
            ssize_t rc;
            nbyte = 0;
            reqmeta.content_len = 0;

            if ((rc = send_headers(browserfd, &target_buf, &io_buf, &reqmeta)) == -1)
                break;
            nbyte += rc;

            if (!send_body(browserfd, &target_buf, &io_buf, reqmeta.content_len))
                break;
            nbyte += reqmeta.content_len;

            /* logging to file */
            format_log_entry(logstring, &browser_addr, reqmeta.uri, nbyte);
            write_log(flogp, logstring);

            DBG(("log to file"));
        }
        Close(browserfd);
        Close(targetfd);

    }

    Close(listenfd);
    fclose(flogp);
    printf("server exits\n");
    exit(0);
}

/**
 *
 * @return  number of byte sent
 *          -1 for error or EOF
 */
ssize_t send_headers(int fd, rio_t *rio_buf, io_buf_t *io_buf, reqmeta_t *reqmeta)
{
    ssize_t nbyte = 0;
    int rc;

    /* add proxy header */
    /* sprintf(io_buf->linebuf, "%s: %s\r\n", "X-SIYUAN-PROXY", "Siyuan Wang");
     * if ((rio_writen(fd, io_buf->linebuf, strlen(io_buf->linebuf)) < 0))
     *     return -1; */

    do {
        if ((rc = get_header(rio_buf, reqmeta, io_buf)) <= 0) /* EOF */
            return -1;

        if ((rc = rio_writen(fd, io_buf->linebuf, strlen(io_buf->linebuf))) < 0) /* error */
            return -1;

        nbyte += strlen(io_buf->linebuf);
    } while (strncmp(CRLF, io_buf->linebuf, 2) != 0);

    return nbyte;
}

/**
 * @return 1 for successfully
 *         0 for error
 */
int send_body(int fd, rio_t *rio_buf, io_buf_t *io_buf, int content_len)
{
    ssize_t nleft = content_len;
    int rc;

    /* 这里要循环的读入bodybuf，因为content_len会比bodybuf大！ */
    while (nleft > 0) {
        if ((rc = rio_readnb(rio_buf, io_buf->bodybuf, nleft > MAXBUF ? MAXBUF : nleft)) < 0)
            return 0;

        if ((rc = rio_writen(fd, io_buf->bodybuf, rc)) < 0) /* error */
            return 0;
        nleft -= rc;
    }

    return 1;
}

void write_log(FILE *fp, char *logstring)
{
    fprintf(fp, "%s\n", logstring);
    fflush(fp);
}

/**
 * read a header in io_buf.linebuf
 * @return
 *         0 for EOF or error
 *         1 for successfully read a header
 */
int get_header(rio_t *rio_buf, reqmeta_t *reqmeta, io_buf_t *io_buf)
{
    /* parse all headers */
    char *delim;

    if (rio_readlineb(rio_buf, io_buf->linebuf, MAXLINE) <= 0)
        return 0;

    /* encounter "Content-Length" means there is payload in the request */
    if (strncmp(CONTENT_LENGTH, io_buf->linebuf, strlen(CONTENT_LENGTH)) == 0) {
        DBG(("%s", io_buf->linebuf));
        delim = strchr(io_buf->linebuf, ':');
        reqmeta->content_len = atoi(delim + 1);
    }

    return 1;
}

/**
 * @return 0 for error or EOF
 *         1 successfully read the body
 */
int get_body(rio_t *rio_buf, io_buf_t *io_buf, int bufsize)
{
    if (rio_readnb(rio_buf, io_buf->bodybuf, bufsize) < 0)
        return 0;
    else
        return 1;
}


/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    DBG(("uri: %s", uri));

    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')
        *port = atoi(hostend + 1);

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        /* pathbegin++; 这里应该是bug */
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
        char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}


