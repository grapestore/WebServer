/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void)
{

  char *buf, *p, *p2, *p3;
  char arg1[MAXLINE], arg2[MAXLINE], value1[MAXLINE], value2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;
  /* Extract the two arguments */
  // getenv함수를 통해 환경변수에 저장해놓은 query_string으로부터 저장된 buf를 받아온다.
  if ((buf = getenv("QUERY_STRING")) != NULL)
  {
    // key=value&key=value가 query_string에 저장되어있어 &로 우선 나눠주고
    p = strchr(buf, '&');
    *p = '\0';
    strcpy(value1, buf);
    strcpy(value2, p + 1);

    // 그다음에 =을통해서 다시 한번나눠줌으로써 value값이 뭔지 알 수 있다.
    p2 = strchr(value1, '=');
    *p2 = '\0';
    strcpy(arg1, p2 + 1);

    p3 = strchr(value2, '=');
    *p3 = '\0';
    strcpy(arg2, p3 + 1);

    // atoi 함수는 argument to integer이라는 함수이다.
    n1 = atoi(arg1);
    n2 = atoi(arg2);
  }
  /* Make the response body */

  //마지막으로 커넥션이 html파일을 클라이언트에 직접 보내준다.
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>",
          content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);
  

  // 또한 통신이 끝났다는 의미로 connection close와 content정보를 
  // 앞에서 보내주지 못했기때문에 마지막에 다시 보내준다.
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}
/* $end adder */
