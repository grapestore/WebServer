#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  // ./tiny port  argc:2 argv[0]:./tiny argv[1]: port
  // argument가 제대로 들어왔는지 check
  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // listen socket 생성 및 listen 상태 
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    //client가 보낸 connect 신호와 연결하여 connect socket 생성 후 clinetfd와 connfd를 연결해준다.
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE], filetype[MAXLINE];
  rio_t rio;

  // read 준비 및 클라이언트가 보낸 header를 읽는다
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);

  // 클라이언트가 보낸 header 정보를 띄어쓰기를 기준으로 method uri version으로 각각 저장한다.
  // request header : GET /url HTTP/1.1
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    clienterror(fd, method, "501", "Not implemented",
                "Tiny does not implement this method");
    return;
  }
  
  // 사실 request header는 한줄이상의 정보를 가지는데
  // 때문에 protocol로 header 정보를 다 보냈다는의미로 마지막에 빈 줄을 보내줌으로써 header 요청이 끝난걸 나타낸다.
  read_requesthdrs(&rio);

  // 들어온 uri를 통해 파일경로(html exe 등)와 매개변수를 받을 수 있다.
  is_static = parse_uri(uri, filename, cgiargs);
  
  // 클라이언트가 요청하는 file이 dir상에 존재하는지 check해준다.
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn’t find this file");
    return;
  }

  // 클라이언트가 HEAD 메소드를 보냈을때 작동하는 조건이며 이럴경우 body내용은 보내지지 않지만
  // body 가 어떤 정보를 가지고있는지까지 tintyheader?정보까지는 보내준다.
  if (!strcasecmp(method, "HEAD"))
  {
    printf("check");
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, sbuf.st_size);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    return;
  }


  // 클라이언트가 GET method를 보낼경우 정적 page와 동적 page를 나누어서 response해준다.
  if (is_static)
  { /* Serve static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn’t read the file");
      return;
    }
    // 정적 page인 경우 web server에서 바로 Return해 주며 끝난다.
    serve_static(fd, filename, sbuf.st_size);
  }
  else
  { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn’t run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  { /* Static content */
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else
  { /* Dynamic content */
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  // 정적 page인경우 일단 파일 내용을 메모리로 올린다.
  // 그 후 disk file은 더이상 필요없으니 close해주며
  // memory에 맵핑된 data를 클라이언트한테 보내주고
  // Munmap함수를 통해 메모리를 반환해준다.
  // 때문에 mmap대신 malloc으로 munmap대신 free를  사용함으로써 대체가 가능하다.
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  if (Fork() == 0)
  { /* Child */
    // CGI 프로그래밍에게 매개변수 key value값을 전달해야하기때문에
    // 환경변수를 이용하여 query_string라는 변수에 key value값을 저장해준다.
    setenv("QUERY_STRING", cgiargs, 1);
    
    // dup2함수를 이용하여 자식프로세스가 표준출력을 바로 클라이언트한테 할 수 있도록 소켓을 연결해준다.
    Dup2(fd, STDOUT_FILENO);              /* Redirect stdout to client */

    //EXCE함수에 종류로 v는 vector e는 환경변수를 의미하는 함수이며
    // 때문에 vector는 필요없으므로 빈리스트를 보내주며 , 환경변수 query_string을 쓸수있게 넘겨준다.
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  // child process가 죽을때까지 아니면 좀비 프로세스가 되니까
  // 반환하기를 기다린다.
  Wait(NULL); /* Parent waits for and reaps child */
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
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
