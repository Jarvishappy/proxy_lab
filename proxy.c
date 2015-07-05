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

#include "csapp.h"
#define DBG(x) do { printf("%s:%d - ", __func__, __LINE__); printf x; \
    putchar('\n'); fflush(stdout); } while(0);

#define MAXHEAD 30
#define CRLF "\r\n"
#define CONTENT_LENGTH "Content-Length"
#define HOST "Host"

/**
 * Represents a HTTP message
 * TODO: 没必要把整个HTTP message全部读到内存里，尤其是header
 * 读完request line之后就有足够的参数connect到target server了，
 * 然后就可以读一行写一行，这样能节省很多内存
 */
typedef struct {
    int content_len;
    char method[10];
    char version[16];
    char host[128];
    int port;
    char path[1024];
    char headers[MAXHEAD][512];
    char body[MAXBUF];
} httpmsg_t;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void write_log(FILE *fp, char *record);
void parse_http_msg(httpmsg_t *httpmsg, int clientfd);

void forward_request(httpmsg_t *httpmsg, int targetfd);
void send_request_line(httpmsg_t *httpmsg, int targetfd);
void send_headers(httpmsg_t *httpmsg, int targetfd);
void send_body(httpmsg_t *httpmsg, int targetfd);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{

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

    /* open listenning fd */
    listenfd = Open_listenfd(port);

    /* char logstring[MAXBUF] = {0}; */
    FILE *flogp = fopen("proxy.log", "w");
    if (!flogp)
        unix_error("fopen");

    printf("server: started on port: %d.\n", port);

    /* we only need two buffer */
    char bodybuf[MAXBUF], linebuf[MAXLINE];
    char logstring[MAXLINE];
    char *delim;

    while (1) {
        browserfd = Accept(listenfd, (struct sockaddr *)&browser_addr, &addrlen);
        DBG(("accept a new connection\n"));
        httpmsg_t httpmsg;
        memset(&httpmsg, 0, sizeof(httpmsg));

        parse_http_msg(&httpmsg, browserfd);

        int targetfd;
        targetfd = Open_clientfd(httpmsg.host, httpmsg.port);

        /* 把请求转发到target server */
        send_request_line(&httpmsg, targetfd);
        send_headers(&httpmsg, targetfd);
        send_body(&httpmsg, targetfd);

        /* 接收target server的response */
        rio_t rio_buf;
        size_t nbyte = 0;
        int len;
        int res_content_len = 0;
        Rio_readinitb(&rio_buf, targetfd);
        /* 一行行读，因为要获取Content-Length */
        do {
            Rio_readlineb(&rio_buf, linebuf, MAXLINE);
            len = strlen(linebuf);
            if (strncmp(CRLF, linebuf, strlen(CRLF)) == 0)
                break;

            if (strncmp(CONTENT_LENGTH, linebuf, strlen(CONTENT_LENGTH)) == 0) {
                delim = strchr(linebuf, ':');
                res_content_len = atoi(delim + 1);
            }

            Rio_writen(targetfd, linebuf, len);
            nbyte += len;
        } while (1);

        /* send response body if needed */
        if (res_content_len > 0) {
            Rio_readnb(&rio_buf, bodybuf, res_content_len);
            Rio_writen(targetfd, bodybuf, res_content_len);
            nbyte += res_content_len;
        }

        if (strncasecmp(httpmsg.path, "http://", 7) == 0)
            strncpy(linebuf, httpmsg.path, strlen(httpmsg.path) + 1);
        else {
            if (httpmsg.port == 0)
                sprintf(linebuf, "http://%s/%s", httpmsg.host, httpmsg.path);
            else
                sprintf(linebuf, "http://%s:%d/%s", httpmsg.host, httpmsg.port, httpmsg.path);
        }

        format_log_entry(logstring, &browser_addr, linebuf, nbyte);
        write_log(flogp, linebuf);
    }

    Close(listenfd);
    fclose(flogp);
    printf("done\n");
    exit(0);
}

void send_request_line(httpmsg_t *httpmsg, int targetfd)
{
    char linebuf[1024] = {0};
    sprintf(linebuf, "%s %s %s", httpmsg->method, httpmsg->path, httpmsg->version);
    Rio_writen(targetfd, linebuf, strlen(linebuf));
}

void send_headers(httpmsg_t *httpmsg, int targetfd)
{
    int i;
    for (i = 0; i < MAXHEAD; i++) {
        if (strlen(httpmsg->headers[i]) <= 0)
            continue;
        Rio_writen(targetfd, httpmsg->headers[i], strlen(httpmsg->headers[i]));
    }
    Rio_writen(targetfd, CRLF, strlen(CRLF));
}

void send_body(httpmsg_t *httpmsg, int targetfd)
{
    if (httpmsg->content_len > 0)
        Rio_writen(targetfd, httpmsg->body, httpmsg->content_len);
}


void parse_http_msg(httpmsg_t *httpmsg, int clientfd)
{
    rio_t rio_buf;
    /* open log file */
    char linebuf[MAXBUF] = {0};
    Rio_readinitb(&rio_buf, clientfd);

    /* read first line */
    if (Rio_readlineb(&rio_buf, linebuf, MAXBUF) == 0) {
        DBG(("client closed socket"));
        exit(1);
    }

    /* parse first line */
    char *p = linebuf;
    char *delim;

    /* get the uri component from HTTP message, preserve trailing "\r\n" */
    while (p != NULL) {
        delim = strchr(p, ' ');
        if (delim != NULL)
            *delim = '\0';

        if (strlen(httpmsg->method) == 0)
            strncpy(httpmsg->method, p, strlen(p) + 1);
        else if (strlen(httpmsg->path) == 0)
            strncpy(httpmsg->path, p, strlen(p) + 1);
        else
            strncpy(httpmsg->version, p, strlen(p) + 1);

        p = delim == NULL ? delim : delim + 1;
    }

    /* parse all headers */
    int i = 0;
    do {
        if (Rio_readlineb(&rio_buf, linebuf, MAXBUF) == 0) {
            DBG(("client closed socket"));
            exit(1);
        }
        /* encounter the CRLF following headers */
        if (strncmp(CRLF, linebuf, strlen(CRLF)) == 0)
            break;

        /* preserve trailing "\r\n" */
        strncpy(httpmsg->headers[i++], linebuf, strlen(linebuf) + 1);

        /* encounter "Content-Length" means there is payload in the request */
        if (strncmp(CONTENT_LENGTH, linebuf, strlen(CONTENT_LENGTH)) == 0) {
            delim = strchr(linebuf, ':');
            httpmsg->content_len = atoi(delim + 1);
        } else if (strncmp(HOST, linebuf, strlen(HOST)) == 0) {
            delim = strchr(linebuf, ':');
            p = delim + 1;
            while (*p == ' ')
                p++;

            /* 看host后面有没有跟着端口号 */
            delim = strchr(p, ':');
            if (delim == NULL) {
                delim = strchr(p, '\r');
                if (delim)
                    *delim = '\0';

                strncpy(httpmsg->host, p, strlen(p) + 1);
            } else {
                *delim = '\0';
                delim += 1;
                strncpy(httpmsg->host, p, strlen(p) + 1);
                httpmsg->port = atoi(delim);
            }
        }


    } while (1);

    /* read all body */
    if (httpmsg->content_len > 0)
        Rio_readnb(&rio_buf, httpmsg->body, httpmsg->content_len);
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
        pathbegin++;
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
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}


