#ifndef NETLIB_H
#define NETLIB_H
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

void unix_error(char *msg) /* unix-style error */
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(0);
}

/**
 *
 * @return -2 for DNS error
 *         -1 for unix error
 */
int open_clientfd(char *host, int port)
{
    int clientfd;
    /* convert host to ip addr via DNS */
    struct hostent *serv_hostp = gethostbyname(host);
    struct in_addr serv_ip;
    char **pp;
    if (NULL == serv_hostp)
        return -2;

    for (pp = serv_hostp->h_addr_list; *pp != NULL; pp++) {
        serv_ip = *((struct in_addr *)*pp);
        break;
    }

    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr = serv_ip;
    serv_addr.sin_port = htons(port);

    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd < 0)
        return -1;


    if (connect(clientfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
        return -1;

    return clientfd;
}


/**
 *
 * @return -1 for unix error
 */
int open_listenfd(int port)
{
    int listenfd, optval = 1;
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) ||
        bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) ||
        listen(listenfd, 1024))
        return -1;

    return listenfd;
}









#endif

