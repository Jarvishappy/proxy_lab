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

/**
 * Represents a HTTP message
 */
typedef struct {
    char method[10];
    char uri[1024];
    char version[16];
    char headers[MAXHEAD][512];
    char body[MAXBUF];
} httpmsg_t;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void write_log(FILE *fp, char *record);

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
    rio_t rio_buf;
    int listenfd, clientfd;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    /* open listenning fd */
    listenfd = Open_listenfd(port);

    /* open log file */
    char linebuf[MAXBUF] = {0};
    /* char logstring[MAXBUF] = {0}; */
    FILE *flogp = fopen("proxy.log", "w");
    if (!flogp)
        unix_error("fopen");

    printf("server: started on port: %d.\n", port);

    while (1) {
        clientfd = Accept(listenfd, (struct sockaddr *)&client_addr, &addrlen);
        DBG(("accept a new connection\n"));
        httpmsg_t httpmsg;
        memset(&httpmsg, 0, sizeof(httpmsg));

        Rio_readinitb(&rio_buf, clientfd);

        /* read first line */
        if (Rio_readlineb(&rio_buf, linebuf, MAXBUF) == 0) {
            DBG(("client close socket"));
            exit(1);
        }

        /* memcpy(httpmsg.uri, linebuf, strlen(linebuf) + 1); */
        /* parse first line */
        char *p = linebuf;
        char *delim;
        while (p != NULL) {
            delim = strchr(p, ' ');
            if (delim != NULL)
                *delim = '\0';

            if (strlen(httpmsg.method) == 0)
                strncpy(httpmsg.method, p, strlen(p) + 1);
            else if (strlen(httpmsg.uri) == 0)
                strncpy(httpmsg.uri, p, strlen(p) + 1);
            else
                strncpy(httpmsg.version, p, strlen(p) + 1);

            p = delim == NULL ? delim : delim + 1;
        }

        /* parse all headers */
        int i = 0;
        do {
            if (Rio_readlineb(&rio_buf, linebuf, MAXBUF) == 0) {
                DBG(("client close socket"));
                exit(1);
            }
            /* preserve trailing "\r\n" */
            strncpy(httpmsg.headers[i++], linebuf, strlen(linebuf) + 1);

        } while (strlen(linebuf) > 2);

        /* get the uri component from HTTP message */
        DBG(("method: %s, uri: %s, version: %s", httpmsg.method, httpmsg.uri, httpmsg.version));
        for (i = 0; i < MAXHEAD; i++) {
            printf("%s", httpmsg.headers[i]);
        }

        break;

    }

    Close(listenfd);
    exit(0);
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


