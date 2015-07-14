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

#include <assert.h>
#include <execinfo.h>
#include <signal.h>
#include "csapp.h"
#define DBG(x) do { printf("%s:%d - ", __func__, __LINE__); printf x; \
    putchar('\n'); fflush(stdout); } while(0);

#define LOG_ERROR(x) do { printf("ERROR: %s:%d - ", __func__, __LINE__); printf x; \
    putchar('\n'); fflush(stdout); } while(0);

#define MAXTHREAD 2
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
typedef struct http_reqline {
    int port;
    char method[5];
    char versionCRLF[10];
    char host[128];
    char path[1024];
    char uri[MAXLINE];
} http_reqline_t;

typedef struct io_buf {
    char linebuf[MAXLINE];
    char bodybuf[MAXBUF];
} io_buf_t;

typedef struct proxy_logger {
    FILE *fp;
    sem_t mutex;
} proxy_logger_t;


typedef void*(thread_func_t)(void*);

/* pthread_create()传给thread的参数 */
typedef struct thread_context {
    int connfd; /* connection fd */
    int tid;    /* thread id */
    struct sockaddr_in conn_addr;
    proxy_logger_t *logger;

} thread_context_t;


void segv_handler(int sig) {
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
void print_err(int err, char *msg);
proxy_logger_t *proxy_logger(const char *filename);
void dolog(proxy_logger_t *loggerp, char *logstring);
void free_proxy_logger(proxy_logger_t *loggerp);
void free_thread_context(thread_context_t *thread_ctxp);
void print_err(int err, char *msg);

/** HTTP message parsing funcitons **/
int parse_uri(char *uri, char *target_addr, char *path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
int parse_request_line(rio_t *rio_buf, http_reqline_t *reqline, io_buf_t *io_buf);
int get_header(rio_t *rio_buf, io_buf_t *io_buf, int *content_lenp);
int get_body(rio_t *rio_buf, io_buf_t *io_buf, int content_len);
int send_reqline(int fd, io_buf_t *iobuf, http_reqline_t *reqline);
ssize_t read_and_send_headers(int fd, rio_t *rio_buf, io_buf_t *io_buf, int *content_lenp);
int read_and_send_body(int fd, rio_t *rio_buf, io_buf_t *io_buf, int content_len);

/** Miscellaneous for multi-thread **/
static int thread_no;
int create_thread(pthread_t *tidp, pthread_attr_t *attrp, thread_func_t *funcp, void *argp);
thread_func_t proxy_worker;

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    signal(SIGSEGV, segv_handler);
    /* ignore SIGPIPE signaled when write to closed socket */
    signal(SIGPIPE, SIG_IGN);

    int port;
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    port = atoi(argv[1]);
    int listenfd, browserfd;
    struct sockaddr_in browser_addr;
    socklen_t addrlen = sizeof(browser_addr);

    init_gethostbyname_mutex();

    /* open listenning fd */
    listenfd = Open_listenfd(port);

    /* all threads shared a logger */
    proxy_logger_t *loggerp = proxy_logger("proxy.log");
    if (!loggerp) {
        LOG_ERROR(("create proxy logger fail"));
        exit(1);
    }

    printf("server: started on port: %d.\n", port);


    while (1) {
        browserfd = Accept(listenfd, (struct sockaddr *)&browser_addr, &addrlen);
        DBG(("accept a new connection"));

        if (thread_no > MAXTHREAD) {
            LOG_ERROR(("thread pool is full, drop new connection"));
            Close(browserfd);
            continue;
        }

        pthread_t tid;
        thread_context_t *thread_ctxp;
        thread_ctxp = malloc(sizeof(thread_context_t));
        if (!thread_ctxp) {
            LOG_ERROR(("error allocating memory for thread context"));
            Close(browserfd);
            continue;
        }

        thread_ctxp->connfd = browserfd;
        thread_ctxp->conn_addr = browser_addr;
        thread_ctxp->tid = thread_no++;
        thread_ctxp->logger = loggerp;

        /* if create thread fail, do cleanning stuff, skip the connection */
        if (!create_thread(&tid, NULL, proxy_worker, (void*)thread_ctxp)) {
            Close(browserfd);
            free_thread_context(thread_ctxp);
            continue;
        }
    }

    Close(listenfd);
    free_proxy_logger(loggerp);
    printf("server exits\n");
    exit(0);
}


/**
 * worker thread logic
 *
 */
void *proxy_worker(void* arg)
{
    Pthread_detach(Pthread_self());
    thread_context_t *thread_ctxp = (thread_context_t*)arg;
    /* states on the thread stack */
    rio_t rbuf;
    io_buf_t iobuf;
    char logstring[MAXLINE];
    int remotefd, rc, content_len;
    ssize_t nbyte = 0;
    http_reqline_t reqline;
    Rio_readinitb(&rbuf, thread_ctxp->connfd);

    while (1) {
        if ((rc = parse_request_line(&rbuf, &reqline, &iobuf)) == 0)
            goto terminate;

        /* connect to remote server */
        remotefd = Open_clientfd(reqline.host, reqline.port);
        /* send request line to remote server */
        if (!send_reqline(remotefd, &iobuf, &reqline))
            goto terminate;

        /* send request to remote */
        content_len = 0;
        if (read_and_send_headers(remotefd, &rbuf, &iobuf, &content_len) == 0 ||
                read_and_send_body(remotefd, &rbuf, &iobuf, content_len) == 0) {
            LOG_ERROR(("send headers or body to remote host fail"));
            goto terminate;
        }

        Rio_readinitb(&rbuf, remotefd);

        /* send response to browser */
        content_len = 0;
        if ((rc = read_and_send_headers(thread_ctxp->connfd, &rbuf,
                        &iobuf, &content_len)) == 0) {
            LOG_ERROR(("send headers to browser fail"));
            goto terminate;
        }
        nbyte += rc;
        if ((rc = read_and_send_body(thread_ctxp->connfd, &rbuf, &iobuf,
                        content_len)) == 0) {
            LOG_ERROR(("send body to browser fail"));
            goto terminate;
        }
        nbyte += rc;

        format_log_entry(logstring, &thread_ctxp->conn_addr, reqline.uri, nbyte);
        dolog(thread_ctxp->logger, logstring);
    }

terminate:
    /* close client connection and remote connection */
    Close(thread_ctxp->connfd);
    Close(remotefd);
    /* reclaim memory */
    free_thread_context(thread_ctxp);

    return 0;
}


/**
 * 解析HTTP Request的第一行，获取目的server的host和port
 */
int parse_request_line(rio_t *rio_buf, http_reqline_t *reqline, io_buf_t *io_buf)
{
    if (Rio_readlineb(rio_buf, io_buf->linebuf, MAXLINE) == 0 ||
        strstr(io_buf->linebuf, "HTTP") == NULL)
        return 0;

    sscanf(io_buf->linebuf, "%s %s %s", reqline->method, reqline->uri, reqline->versionCRLF);
    parse_uri(reqline->uri, reqline->host, reqline->path, &reqline->port);

    return 1;
}

/**
 *
 * @return 0 for error
 */
int send_reqline(int fd, io_buf_t *iobuf, http_reqline_t *reqline)
{
    sprintf(iobuf->linebuf, "%s %s %s", reqline->method, reqline->path, reqline->versionCRLF);
    if (rio_writen(fd, iobuf->linebuf, strlen(iobuf->linebuf)) < 0) {
        print_err(errno, "rio_writen");
        return 0;
    }
    return 1;
}

/**
 * read headers from rio_buf and wirte to fd
 * @return  number of byte sent
 *          0 for error or EOF
 */
ssize_t read_and_send_headers(int fd, rio_t *rio_buf, io_buf_t *io_buf, int *content_lenp)
{
    ssize_t nbyte = 0;
    int rc;

    do {
        if ((rc = get_header(rio_buf, io_buf, content_lenp)) <= 0) /* EOF */
            return 0;

        if ((rc = rio_writen(fd, io_buf->linebuf, strlen(io_buf->linebuf))) < 0) { /* error */
            print_err(errno, "rio_writen");
            return 0;
        }

        nbyte += rc;
    } while (strncmp(CRLF, io_buf->linebuf, 2) != 0);

    return nbyte;
}

/**
 * @return 1 for successfully
 *         0 for error
 */
int read_and_send_body(int fd, rio_t *rio_buf, io_buf_t *io_buf, int content_len)
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

proxy_logger_t *proxy_logger(const char *filename)
{
    proxy_logger_t *loggerp = malloc(sizeof(proxy_logger_t));
    if (!loggerp) {
        LOG_ERROR(("allocate memory for proxy logger fail"));
        return NULL;
    }


    FILE *fp = fopen(filename, "a");
    if (!fp) {
        print_err(errno, "open proxy log fail");
        fclose(fp);
        exit(1);
    }

    loggerp->fp = fp;
    /* OSX好像没有实现这个函数 */
    if (sem_init(&loggerp->mutex, 0, 1) < 0) {
        print_err(errno, "sem_init");
        return NULL;
    }

    return loggerp;
}

void dolog(proxy_logger_t *loggerp, char *logstring)
{
    sem_wait(&loggerp->mutex);
    fprintf(loggerp->fp, "%s\n", logstring);
    fflush(loggerp->fp);
    sem_post(&loggerp->mutex);
}

void free_proxy_logger(proxy_logger_t *loggerp)
{
    if (NULL == loggerp)
        return;

    fclose(loggerp->fp);
    free(loggerp);
}

void free_thread_context(thread_context_t *thread_ctxp)
{
    if (NULL == thread_ctxp)
        return;

    free(thread_ctxp);
    thread_no--;
}

void print_err(int err, char *msg)
{
    char errbuf[256];
    strerror_r(err, errbuf, 256);
    fprintf(stderr, "%s: %s\n", msg, errbuf);
}


/**
 * wrapper for pthread_create()
 * @return 0 for error
 */
int create_thread(pthread_t *tidp, pthread_attr_t *attrp, thread_func_t *funcp, void *argp)
{
    int rc;
    if ((rc = pthread_create(tidp, attrp, funcp, argp)) != 0) {
        print_err(rc, "pthread_create error");
        return 0;
    }

    return 1;
}



/** HTTP message related functions **/

/**
 * read a header in io_buf.linebuf
 * @return
 *         0 for EOF or error
 *         1 for successfully read a header
 */
int get_header(rio_t *rio_buf, io_buf_t *io_buf, int *content_lenp)
{
    /* parse all headers */
    char *delim;

    if (rio_readlineb(rio_buf, io_buf->linebuf, MAXLINE) <= 0)
        return 0;

    /* encounter "Content-Length" means there is payload in the request */
    if (strncmp(CONTENT_LENGTH, io_buf->linebuf, strlen(CONTENT_LENGTH)) == 0) {
        assert (*content_lenp == 0);
        DBG(("%s", io_buf->linebuf));
        delim = strchr(io_buf->linebuf, ':');
        assert (delim != NULL);
        *content_lenp = atoi(delim + 1);
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


