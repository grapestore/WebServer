/* Master version
 * 15213 2013 Fall
 * Proxy Lab
 * Name: Enze Li
 * Andrew id: enzel
 * ----------------------
 * 
 *
 *
 */

#include <stdio.h>
#include "csapp.h"
// #include "cache.h"

#define DEFAULT_PORT 80

/* You won't lose style points for including these long lines in your code */
static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_ = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection = "Cnnection: close\r\n";
static const char *proxy_connection = "Proxy-connection: close\r\n";

void *proxy_thread(void *vargp);
void proxy(int cliendfd);
// void write_requesthdrs(rio_t *rp, char *newreq);
void write_requesthdrs(char *newreq, char *host);
int parse_uri(char *uri, char *host, char *file);
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);

// cache_t *mycache;

int main(int argc, char **argv)
{
    int listenfd, *connfdp, port;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t pid;    

    if (argc != 2){
    	fprintf(stderr, "Proxy usage: %s <port>\n", argv[0]);
    }

    /* Ignore broken pipe) */
    Signal(SIGPIPE, SIG_IGN);

    /* parse port */
    port = atoi(argv[1]);

    /* listen for connection */
    if ((listenfd = Open_listenfd(port)) < 0) {
        fprintf(stderr, "Open_listenfd error: %s\n", strerror(errno));
        return 0;
    }

    while (1) {
    	clientlen = sizeof(clientaddr);
    	// connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    	// proxy(connfd);
    	// Close(connfd);

        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&pid, NULL, proxy_thread, connfdp);

    }


    return 0;
}

/*
 * proxy_thread - proxy multi threading
 */
 
void *proxy_thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    proxy(connfd);
    Close(connfd);
    return NULL;
}


/*
 * proxy - handle the proxy operations for a client 
 * 1. Get HTTP request and header information from client
 * 2. Forward the request and header information to the server
 * 3. Get response from server and forward it back to client
 */
void proxy(int cliendfd)
{
	int hostport, serverfd;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], file[MAXLINE], newreq[MAXLINE], response[MAXLINE];
	rio_t client_rio, server_rio;
    size_t n;

	/* get request for client */
    rio_readinitb(&client_rio, cliendfd);
    if (rio_readlineb(&client_rio, buf, MAXLINE) < 0) {
        fprintf(stderr, "Error reading from client.");
        return;
    }

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        fprintf(stderr, "HTTP request error.");
        return;
    }

    if (strcasecmp(method, "GET") != 0) {
        clienterror(cliendfd, method, "501", "Not Implemented",
                "Proxy only supports GET method");
        return;
    }

    /* write up the new http request */
    hostport = parse_uri(uri, host, file);
    sprintf(newreq, "GET %s HTTP/1.0\r\n", file);
    // write_requesthdrs(&client_rio, newreq); 
    write_requesthdrs(newreq, host); 
    // printf("The request sent to sever is:\n%s\n", newreq);


    /* Forward the request to the server */    
    if ((serverfd = Open_clientfd(host, hostport)) == -1){
        printf("Cannot connect to web server.\n");
    	clienterror(cliendfd, host, "400", "Bad Request",
                    "The host name or port number maybe invalid");
        return;
    }

    rio_writen(serverfd, newreq, strlen(newreq));

    /* Forward response from server to client */
    rio_readinitb(&server_rio, serverfd);
    while ((n = rio_readlineb(&server_rio, response, MAXLINE)) > 0){
        rio_writen(cliendfd, response, n);
    }

    Close(serverfd);
}


/*
 * write_requesthdrs - write required HTTP request headers
 */

void write_requesthdrs(char *newreq, char *host) 
{
    sprintf(newreq, "%sHost: %s\r\n",newreq, host);
    sprintf(newreq, "%s%s", newreq, user_agent);
    sprintf(newreq, "%s%s", newreq, accept_);
    sprintf(newreq, "%s%s", newreq, accept_encoding);
    sprintf(newreq, "%s%s", newreq, connection);
    sprintf(newreq, "%s%s\r\n", newreq, proxy_connection);
    return;
}

/*
void write_requesthdrs(rio_t *rp, char *newreq) 
{
    char buf[MAXLINE];
    char host[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n") != 0) {
        if ((Rio_readlineb(rp, buf, MAXLINE)) < 0){
            return;
        }
        if (strstr(buf, "Host:")) {
            sscanf(buf, "%*[^:]: %s", host);
            sprintf(newreq, "%sHost: %s\r\n",newreq, host);
        }
    }
    sprintf(newreq, "%s%s", newreq, user_agent);
    sprintf(newreq, "%s%s", newreq, accept_);
    sprintf(newreq, "%s%s", newreq, accept_encoding);
    sprintf(newreq, "%s%s", newreq, connection);
    sprintf(newreq, "%s%s", newreq, proxy_connection);
    return;
}
*/


/*
 * parse_uri - get host, file path from uri
 *        return port if specified, else return 80
 */
int parse_uri(char *uri, char *host, char *file) 
{
    int port;
    char buf[MAXLINE];

    sscanf(uri, "%*[^:]://%[^/]%s", host, file);
    if (strstr(host, ":")) {
        strcpy(buf, host);
        sscanf(buf, "%[^:]:%d", host, &port);
        return port;
    }

    return DEFAULT_PORT;
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}