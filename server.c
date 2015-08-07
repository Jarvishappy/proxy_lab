/**
 * Echo server
 *
 * listening on port 8888,
 * receive msg from client,
 * log 
 *
 */

#include "csapp.h"

#define PORT        8888
#define BUFSIZE     256
#define HELLO "Hello"


int main(int argc, char *argv[])
{
    int listenfd = Open_listenfd(PORT);

    int clientfd;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buf[BUFSIZE] = {0};
    int nread;
    rio_t rbuf;


    printf("server: started on port: %d.\n", PORT);

    while (1) {
        clientfd = Accept(listenfd, (struct sockaddr *)&client_addr, &addrlen);
        Rio_readinitb(&rbuf, clientfd);
        printf("server: accept a new connection\n");

        while ((nread = Rio_readlineb(&rbuf, buf, BUFSIZE)) > 0) {
            printf("server: received \"%s\", nread: %d\n", buf, nread);

            Rio_writen(clientfd, buf, strlen(buf));
        }

        if (nread == -1)
            unix_error("rio_readlinen");
        else if (nread == 0) { /* client socket has been closed */
            printf("server: client socket has been closed.\n");

            Close(clientfd);
        }
    }


    Close(listenfd);

    return 0;
}
