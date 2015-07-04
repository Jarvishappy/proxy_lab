/**
 * Echo server
 *
 * listening on port 8888,
 * receive msg from client,
 * log 
 *
 */

#include "netlib.h"
#include "rio.h"

#define PORT        8888
#define BUFSIZE     256


int main(int argc, char *argv[])
{
    int listenfd = open_listenfd(PORT);

    int clientfd;
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buf[BUFSIZE] = {0};
    int nread;


    printf("server: started on port: %d.\n", PORT);

    while (1) {
        clientfd = accept(listenfd, (struct sockaddr *)&client_addr, &addrlen);
        if (clientfd < 0)
            unix_error("accept");
        printf("server: accept a new connection\n");

        while ((nread = rio_readlinen(clientfd, buf, BUFSIZE)) > 0) {
            printf("server: received \"%s\", nread: %d\n", buf, nread);
            if (rio_writen(clientfd, buf, nread) < 0)
                unix_error("rio_writen");
        }

        if (nread == -1)
            unix_error("rio_readlinen");
        else if (nread == 0) { /* client socket has been closed */
            printf("server: client socket has been closed.\n");

            if (close(clientfd) != 0)
                unix_error("close");
        }
    }




    return 0;
}
