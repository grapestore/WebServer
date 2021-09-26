#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
/* Recommended max cache and object sizes */

//cache의 전체 사이즈
#define MAX_CACHE_SIZE 1049000

//하나의 struct의 사이즈 100kb다
#define MAX_OBJECT_SIZE 102400

// 가장 최근에 업데이트된 cache page index를 의미함
#define LRU_MAGIC_NUMBER 9999

// 10개의 cache 저장소를 만들었다.
#define CACHE_OBJS_COUNT 10

// 만일 클라이언트가 보낸 header정보가 없다면 proxy서버에서 저장해놓은 header정보를 보내줌으로써
// tiny가 브라우저 정보를 확인 할 수 있다.
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";
void *thread(void *vargp);

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);
/*cache function*/
void cache_init();
int cache_find(char *url);
int cache_eviction();
void cache_LRU(int index);
void cache_uri(char *uri,char *buf);
void readerPre(int i);
void readerAfter(int i);

typedef struct {
    // 페이지에 정보를 저장하는 멤버
    char cache_obj[MAX_OBJECT_SIZE];

    // 페이지 url을 저장하는 멤버
    char cache_url[MAXLINE];
    int LRU;
    // 해당 cache가 비었는지를 의미 1이 비었으며 0이 차있음
    int isEmpty;

    //접근하여 있는 thread의 수(reader thread)
    int readCnt;

    // 세마포어키로 되어있는 write mutex 키            
    sem_t wmutex;           /*protects accesses to cache*/
    sem_t rdcntmutex;       /*protects accesses to readcnt*/

    int writeCnt;
    sem_t wtcntMutex;
    sem_t queue;

}cache_block;

typedef struct {
  // 10개의 빈 캐쉬 생성
    cache_block cacheobjs[CACHE_OBJS_COUNT];  /*ten cache blocks*/
    int cache_num;
}Cache;

Cache cache;


int main(int argc,char **argv)
{
    int listenfd;
    void *connfdp;
    socklen_t  clientlen;
    char hostname[MAXLINE],port[MAXLINE];
    pthread_t tid;
    struct sockaddr_storage clientaddr;/*generic sockaddr struct which is 28 Bytes.The same use as sockaddr*/

    // cache 초기화
    cache_init();

    if(argc != 2){
        fprintf(stderr,"usage :%s <port> \n",argv[0]);
        exit(1);
    }

    // ? 아직 잘모르겠다 process가 죽으면 신호를 보내는걸로아는데 thread도 죽으면 보내는 신호를 무시하는걸까?
    Signal(SIGPIPE,SIG_IGN);

    // 클라이언트 입장에서 proxy가 서버이므로 listen socket 생성
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        // 아직 수정되지 않았지만 connfd 가 겹칠수있는 일이 생길 수 있기때문에
        // malloc을 이용하여 변경해주어야 한다.
        connfdp = malloc(sizeof(int))
        *connfdp = Accept(listenfd,(SA *)&clientaddr,&clientlen);

        /*print accepted message*/
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        printf("Accepted connection from (%s %s).\n",hostname,port);

        /*concurrent request*/
        // 동시성 프로그래밍을 위해 thread를 생성해주는 함수이다 (쓰레드 id, null, 실행할함수, 함수에 넘겨줄 매개변수)
        Pthread_create(&tid,NULL,thread,connfdp);
    }
    return 0;
}

/*thread function*/
void *thread(void *vargp){
  // 각 thread마다 각자의 클라이언트와 연결해줄수있는 file(socket)을 연결해준다.
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    free(vargp);
    doit(connfd);
    Close(connfd);
}

/*handle the client HTTP transaction*/
// 각 thread가 처리할 행동
void doit(int connfd)
{
    int end_serverfd;/*the end server file descriptor*/

    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char endserver_http_header [MAXLINE];

    /*store the request line arguments*/
    char hostname[MAXLINE],path[MAXLINE];
    int port;

    rio_t rio,server_rio;/*rio is client's rio,server_rio is endserver's rio*/

    //client와 연결할(읽을) 준비를 해준다.
    Rio_readinitb(&rio,connfd);
    //client로부터 header정보를 읽어들인다.
    Rio_readlineb(&rio,buf,MAXLINE);
    sscanf(buf,"%s %s %s",method,uri,version); /*read the client request line*/

    char url_store[100];
    // url_store 배열에 클라이언트가 요청한 uri를 저장해준다.
    strcpy(url_store,uri);  /*store the original url */

    // 멀쩡한 method가 들어왔는지 check 만일 get이 아닌 다른게 들어왔으면 1이상의 값을 보내줌으로써 true처리
    if(strcasecmp(method,"GET")){
        //printf("Proxy does not implement the method\n");
        return;
    }

    // 만일 클라이언트가 요청한 uri가 이미 cache되어 있다면 보내주고 끝낸다.
    int cache_index;
    if((cache_index=cache_find(url_store))!=-1){/*in cache then return the cache content*/
        // 각각의 thread가 cache에 접근할때 본인이 접근한 cache를 semaphore로 token처리함으로써
        // 다른 thread가 접근 access하는것을 막아준다. 만일 공유변수로써 읽는도중에 다른 thread가 접근할수있다면
        // 현재 접근한 thread가 잘못된값을 가져갈수있게된다.
         readerPre(cache_index);
         Rio_writen(connfd,cache.cacheobjs[cache_index].cache_obj,strlen(cache.cacheobjs[cache_index].cache_obj));
         // thread가 cache에 볼일이 끝났으면 다썻다고 mutex를 1로 반환해준다.
         readerAfter(cache_index);

         // cache page가 얼마나 자주 호출됬는지 업데이트해주기위해 사용
         // 자신의 LRU는 9999로 만들고 나머지 cache의 LRU는 -1씩해줌으로써
         // 어떤 cache가 비번하게 호출되는지 알 수 있다.
         cache_LRU(cache_index);
         return;
    }

    /*parse the uri to get hostname,file path ,port*/
    parse_uri(uri,hostname,path,&port);

    /*build the http header which will send to the end server*/
    // 클라이언트가 보낸 header가 없는경우 proxy서버에 저장되어있는 header정보가
    // tiny server한테 날아가게 된다.
    build_http_header(endserver_http_header,hostname,path,port,&rio);

    // proxy server입장에서는 본인이 client고 tiny가 server이기 때문에 clinet socket을 생성해준다.
    end_serverfd = connect_endServer(hostname,port,endserver_http_header);
    // 만일 tiny와 연결이 안되었다면 connfd socket이 생성이 안되었으므로 -1을 return 해준다.
    if(end_serverfd<0){
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio,end_serverfd);

    /*write the http header to endserver*/
    Rio_writen(end_serverfd,endserver_http_header,strlen(endserver_http_header));

    /*receive message from end server and send to the client*/
    char cachebuf[MAX_OBJECT_SIZE];
    int sizebuf = 0;
    size_t n;

    // tiny가 보낸 buf를 한줄씩읽으면서 cachebuf에 한줄씩 붙여준다.
    while((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0)
    {
        sizebuf+=n;
        // 총 버퍼의 길이가 cache의 크기보다 작을때 까지만 cachebuf에 저장시켜주며
        if(sizebuf < MAX_OBJECT_SIZE)  strcat(cachebuf,buf);
        Rio_writen(connfd,buf,n);
    }

    Close(end_serverfd);

    // 만약 총 buf가 cache의 허용치를 넘는다면 cache에 저장해주지 않는다.
    if(sizebuf < MAX_OBJECT_SIZE){
      // 저장할수 있는 url buf인 경우
      // 희생자 cache를 찾아서 대체해주거나 희생자 cache는 LRU값이 가장 낮은에가
      // 가장 빈번하지않게 호출되는 page정보이므로 가장 작은애를 희생자로 정한다.
      // 아직 cache에 남는자리가 있다면 그자리에 바로 url정보와 html정보들을 넣어준다.
        cache_uri(url_store,cachebuf);
    }
}

void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio)
{
    char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
    /*request line*/
    sprintf(request_hdr,requestlint_hdr_format,path);
    /*get other request header for client rio and change it */
    while(Rio_readlineb(client_rio,buf,MAXLINE)>0)
    {
        if(strcmp(buf,endof_hdr)==0) break;/*EOF*/

        if(!strncasecmp(buf,host_key,strlen(host_key)))/*Host:*/
        {
            strcpy(host_hdr,buf);
            continue;
        }

        if(!strncasecmp(buf,connection_key,strlen(connection_key))
                &&!strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key))
                &&!strncasecmp(buf,user_agent_key,strlen(user_agent_key)))
        {
            strcat(other_hdr,buf);
        }
    }
    if(strlen(host_hdr)==0)
    {
        sprintf(host_hdr,host_hdr_format,hostname);
    }
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);

    return ;
}
/*Connect to the end server*/
inline int connect_endServer(char *hostname,int port,char *http_header){
    char portStr[100];
    sprintf(portStr,"%d",port);
    return Open_clientfd(hostname,portStr);
}

/*parse the uri to get hostname,file path ,port*/
void parse_uri(char *uri,char *hostname,char *path,int *port)
{
    *port = 80;
    char* pos = strstr(uri,"//");

    pos = pos!=NULL? pos+2:uri;

    char*pos2 = strstr(pos,":");
    if(pos2!=NULL)
    {
        *pos2 = '\0';
        sscanf(pos,"%s",hostname);
        sscanf(pos2+1,"%d%s",port,path);
    }
    else
    {
        pos2 = strstr(pos,"/");
        if(pos2!=NULL)
        {
            *pos2 = '\0';
            sscanf(pos,"%s",hostname);
            *pos2 = '/';
            sscanf(pos2,"%s",path);
        }
        else
        {
            sscanf(pos,"%s",hostname);
        }
    }
    return;
}
/**************************************
 * Cache Function
 **************************************/

void cache_init(){
    cache.cache_num = 0;
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        cache.cacheobjs[i].LRU = 0;
        cache.cacheobjs[i].isEmpty = 1;
        Sem_init(&cache.cacheobjs[i].wmutex,0,1);
        Sem_init(&cache.cacheobjs[i].rdcntmutex,0,1);
        cache.cacheobjs[i].readCnt = 0;

        cache.cacheobjs[i].writeCnt = 0;
        Sem_init(&cache.cacheobjs[i].wtcntMutex,0,1);
        Sem_init(&cache.cacheobjs[i].queue,0,1);
    }
}

void readerPre(int i){
  // queue mutex를 통해 reader와 writer중에 우선순위를 골고루 줄 수 있다.
  // 만일 다른 thread가 해당캐쉬를 이미 쓰고있다고 queue를 Lock함으로써
  // 현재 thread는 그 정보를 다른 thread가 전부 작성 해줄때까지 열람할수없다.
    P(&cache.cacheobjs[i].queue);
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt++;
    // reader thread가 읽을 예정이니 쓰기를 lock 걸어준다.
    if(cache.cacheobjs[i].readCnt==1) P(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
    V(&cache.cacheobjs[i].queue);
}

void readerAfter(int i){
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt--;
    if(cache.cacheobjs[i].readCnt==0) V(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);

}

void writePre(int i){
    P(&cache.cacheobjs[i].wtcntMutex);
    cache.cacheobjs[i].writeCnt++;
    //쓰기 thread가 쓸 예정이니 reader가 접근 해서 잘못된 값을 가져가지 않게하도록 막아준다.
    if(cache.cacheobjs[i].writeCnt==1) P(&cache.cacheobjs[i].queue);
    V(&cache.cacheobjs[i].wtcntMutex);
    P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i){
    V(&cache.cacheobjs[i].wmutex);
    P(&cache.cacheobjs[i].wtcntMutex);
    cache.cacheobjs[i].writeCnt--;
    // 작성 완료하였으므로 queue mutex를 unlock해준다.
    if(cache.cacheobjs[i].writeCnt==0) V(&cache.cacheobjs[i].queue);
    V(&cache.cacheobjs[i].wtcntMutex);
}

/*find url is in the cache or not */
int cache_find(char *url){
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        readerPre(i);
        if((cache.cacheobjs[i].isEmpty==0) && (strcmp(url,cache.cacheobjs[i].cache_url)==0)) break;
        readerAfter(i);
    }
    if(i>=CACHE_OBJS_COUNT) return -1; /*can not find url in the cache*/
    return i;
}

/*find the empty cacheObj or which cacheObj should be evictioned*/
int cache_eviction(){
    int min = LRU_MAGIC_NUMBER;
    int minindex = 0;
    int i;
    for(i=0; i<CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        if(cache.cacheobjs[i].isEmpty == 1){/*choose if cache block empty */
            minindex = i;
            readerAfter(i);
            break;
        }
        if(cache.cacheobjs[i].LRU< min){    /*if not empty choose the min LRU*/
            minindex = i;
            readerAfter(i);
            continue;
        }
        readerAfter(i);
    }

    return minindex;
}
/*update the LRU number except the new cache one*/
void cache_LRU(int index){

    writePre(index);
    cache.cacheobjs[index].LRU = LRU_MAGIC_NUMBER;
    writeAfter(index);

    int i;
    for(i=0; i<index; i++)    {
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0 && i!=index){
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
    i++;
    for(i; i<CACHE_OBJS_COUNT; i++)    {
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0 && i!=index){
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
}
/*cache the uri and content in cache*/
void cache_uri(char *uri,char *buf){

    // 희생자를 정하는 함수 만일 cache가 비어있으면 비어있는 index를 반환한다.
    int i = cache_eviction();

    // 쓰기전에 cache를 lock걸어준다.
    writePre(i);/*writer P*/

    strcpy(cache.cacheobjs[i].cache_obj,buf);
    strcpy(cache.cacheobjs[i].cache_url,uri);
    cache.cacheobjs[i].isEmpty = 0;

    // 작성이 끝났으므로 cache unlock
    writeAfter(i);/*writer V*/

    // 새로운 page가 불러졌으므로 LRU값을 다시 9999로 올려준다.
    cache_LRU(i);
}
