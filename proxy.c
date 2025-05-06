#include <stdio.h>
#include <locale.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


void *thread_routine(void *connfdp);
void doit(int fd);
int parse_uri(char *uri, char *hostname, char *path, int *port);
void makeHTTPheader(char *http_header, char *hostname, char *path, int port , rio_t *client_rio);


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

//proxy의 main 알고리즘과 doit 상단은 tiny와 거의 동일함

/*
=====================================================================
[main 함수]

결국 클라이언트에게 요청을 받아서 네트워크적으로는 서버 역할을 수행
=> 클라이언트 연결을 받고 처리하는 기본 서버 알고리즘은 비슷할 수 밖에 없음

클라이언트가 접속 => accept() => 요청 처리
======================================================================

======================================================================
[doit 함수]

- 두 서버 모두 클라이언트에게서 HTTP 요청을 받는다는 점이 같음
- 요청 라인을 받아 파싱하는 기본 작업이 동일하다.
  => 서버 입장에서 보면 클라이언트 요청을 해석하는 작업이 본질적으로 같음 ! 
======================================================================
*/
int main(int argc, char **argv) { 
  setlocale(LC_ALL, "ko_KR.utf8");
  /*
==================================================================
  char **argv는 명령줄에서 입력된 문자열 인자들을 배열 형태로
  다루기 위해 사용하는 이중 포인터 

  - argv는 문자열 포인터 배열이고
  - 그래서 타입이 char **argv (이중 포인터)

  [예시]
  ./tiny 8000
  ./proxy 15213

  => tiny와 proxy 둘 다 인자를 문자열로 받아서 처리하므로 구조가 same

  argc=2

  argv[0] => "./proxy"
  argv[1] => "15123"
  argv[2] => NULL (끝 표시)
======================================================================
  */


  // 진짜 tiny의 main이랑 똑같음 ! 

    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid; 
    /*
     *===============================================================
     * -csapp.h 에 정의되어 있음
     * -POSIX 스레드 라이브러리에서 스레드를 식별하는데 쓰이는 식별자 타입
     * -이 타입은 스레드를 생성할 때 반환되는 ID를 저장하는데 사용됨
     *=============================================================== 
    */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
 
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {

        /*
        ======================================================

        - server main에서 일련의 처리를 하는 대신 스레드를 분기
        - 각 연결에 대해 이후의 과정은 스레드 내에서 병렬적 처리
        - main은 다시 while문의 처음으로 돌아가 새로운 연결 wait

        - 이때의 각 스레드는 모두 각각의 connfd(연결)를 가져야 하기 때문에
        - 연결마다 '메모리를 할당' 하여 포인팅한다. malloc 할당 해주기 
        
        ======================================================
        */
        clientlen = sizeof(clientaddr);
        int *connfdp = Malloc(sizeof(int));
        //connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        //doit(connfd);
        //Close(connfd);
        Pthread_create(&tid,NULL,thread_routine,connfdp);
        // 새 쓰레드 생성 thread_routine() 함수에서 연결 처리 해준다
        // 인자로 넘기는건  connfdp (할당한 소켓 fd 포인터)
    }

  printf("%s", user_agent_hdr);
  return 0;

}

void *thread_routine(void *connfdp){
  /*
  =========================================================================
  - 각 스레드별 connfd는 입력으로 가져온 connfdp가 가리키던 할당된 위치의 fd값
  - 스레드 종료시 자원을 반납하고 
  - connfdp도 이미 connfd를 얻어 역할을 다했으니 반납해 주어야 한다.
  =========================================================================
  
  */

 int connfd = *((int *) connfdp);
 Pthread_detach(pthread_self());

 Free(connfdp); // malloc 으로 메모리 할당해준거 free해주기
 doit(connfd); // 프록시에서 일단 tiny 서버로 보내주기 !
 Close(connfd); // 연결 종료 시켜주기 
 return NULL; 
}




void doit(int fd){
  //전체적인 통신을 처리하는 함수 ! 
  char buf[MAXLINE], method[MAXLINE],uri[MAXLINE],version[MAXLINE];
  char HTTPheader[MAXLINE], hostname[MAXLINE],path[MAXLINE]; //출력용 버퍼
  rio_t rio;

  //프록시 서버에서 "백엔드 서버"와 통신할 때 사용하는 변수
  //클라이언트가 아닌 "원격 서버 쪽"과 연결된 입출력 버퍼
  int backfd;
  rio_t backrio; 

  //tiny 코드 부분이랑 동일 !  
  //프록시나 웹서버에서 "클라이언트의 HTTP 요청을 읽고 파싱" 하는 핵심 부분

  Rio_readinitb(&rio, fd); //robust I/O를 사용하기 위한 초기화
  Rio_readlineb(&rio, buf, MAXLINE); //클라이언트가 보낸 HTTP 요청라인의 첫 줄 읽기
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); 
  /*
  파싱해서 각각의 변수에 저장해준다. 
  요청의 첫 줄 GET http://host/path HTTP/1.0 읽기
  */

  if (strcasecmp(method,"GET"))
  {

    printf("Proxy가 해당 요청을 처리할 수 없습니다.");
    //원래 tiny에서는 clienterror로 처리해 줬었음
    //GET 이외의 요청 거부 
    return;
  }
  int port;
  /*
  =============================================================
  uri를 파싱하는 목적은 서버마다 다른데,Proxy 서버에서의 목적은
  hostname과 path를 추출하고 포트를 결정하는 목적

  => 아래에서 이 목적에 다르는 코드로 parse_uri를 구현한다.
  =============================================================
  */

  parse_uri(uri,hostname,path,&port);
  //http://hostname:port/path 형태에서 함수 호출을 통해 분리
  //1차적으로 분리 해준 뒤에 아래의 makeHTTPheader 함수에서 응용해서 요청생성

  makeHTTPheader(HTTPheader,hostname,path,port,&rio);
  //결정된 hostname,path,port에 따라 HTTP header를 만든다.
  //proxy가 웹 서버에 보낼 헤더 구성하기 함수 호출을 통해 구현 

  /*============================
   *  서버와 연결하는 부분 추가 
   *=============================
  */

  char portch[10];
  sprintf(portch,"%d",port);
  // 서버와 연결(back) 후 만든 HTTP header를 만든다.
  //portch는 URI 문자열에서 포트번호가 시작되는 부분을 가리키는 포인터
  backfd = Open_clientfd(hostname,portch);
  if( backfd < 0 ) //Open_clientfd 반환값으로 음수 값이면 오류임 !
  //소켓 생성과 연결 과정을 간단하게 묶은 함수
  {
    printf("연결에 실패했습니다.");
    return;
    
  }
  Rio_readinitb(&backrio,backfd);
  //proxy가 tiny 서버와 통신할 robust I/O 버퍼를 초기화


  Rio_writen(backfd,HTTPheader,strlen(HTTPheader));
  /*
  =========================================
  프록시가 tiny서버에 HTTP 요청 헤더를 보낸다.
  =========================================
  */

/*==========================================
 *     서버의 요청 응답을 한 줄씩 읽고
 *       클라이언트에게 전달 해준다.
 *===========================================
  */
  size_t n;
  while((n = Rio_readlineb(&backrio,buf,MAXLINE)) != 0)
  {

    printf("proxy가 %d bytes를 받고 ,전달합니다.\n",n);
    Rio_writen(fd,buf,n);

  }
  Close(backfd);
  //서버와의 연결 종료 해주기 

}



int parse_uri(char *uri, char *hostname, char * path, int *port){

/*
===============================================
-uri : 입력값
-hostname : 출력용 버퍼
-path : 출력용 버퍼
- &port : 출력값을 담을 정수 포인터

parse_uri()는 이 모든걸 수정해서 반환하는 함수 
===============================================
*/

*port = 80; //기본 포트는 80을 쓴다. 
 char *hostnameP =strstr(uri,"//"); // uri안에 "//"가 있는지 검사하기
 //문자하나하나가 인덱스다 배열의 , 문자열이 배열이다. 

 /*
 ======================================================================
 "http://www.example.com:8080/index.html" 이라고 할 때 
 맨 처음 // 찾아서 가기

 strncpy (범위를 잡아주는 문자열 함수)
 :이 없으면 . . . port 번호가 null 일때는 path의 포인터에서 hostname포인터를 빼주면 
 
 ======================================================================
 */
 
 if(hostnameP != NULL){
  hostnameP =hostnameP + 2; //시작점만 들고있음 슬래시 필요없음 임의로 W부터 시작하도록 
 }
 else{
  //없다면 uri의 처음부터 
  hostnameP = uri; // www부터 시작했다는거임
 }
 
 //path는 hostnameP의 ':' 부터 !
 char *pathP =strstr(hostnameP,":"); //부분 문자열 찾기

 // :8080 처럼 :가 있는지 확인하고 ( 포트 명시 여부 )

 if(pathP != NULL){
  //포트가 명시된 경우에는 추출해서 저장해주기

  *pathP = '\0';
  sscanf(hostnameP,"%s",hostname); //"www.example.com" 까지
  sscanf(pathP + 1,"%d%s",port,path); // : 포트와 path 추출하기 : 다음으로 넘어가기 
  //strcpy로 hostnameP의 범위를 잡아줌 (미니 준혁) => 범위만큼 담아주기 

 }
 else{
/*
=================================================================
포트번호 없이 /path만 있는경우 http://www.exapmle.com/index.html
포트는 기본값 80 사용하기

':'가 없다면 '/'를 찾는다.
=================================================================
*/
   
  pathP =strstr(hostnameP,"/");
  if(pathP != NULL){
    *pathP = '\0';
    sscanf(hostnameP,"%s",hostname); //덮어쓰기 해준 상태  
    *pathP = '/';
    sscanf(pathP,"%s",path);
  }
  else{

    /*
    =========================================
    ':'도 '/'도 없다면 hostname만 카피
    http://www.example.com 
    hostname = "www.example.com" 만 추출하기 
    port는 80 path는 빈 문자열 
    =========================================
    */

    sscanf(hostnameP,"%s",hostname);
  }
 }
 return 0;
}

//이 문자열 들을 조합해서 프록시가 서버에 보낼 전체 요청을 구성 한다. 
//실제 전송될 헤더 문자열들
static const char *user_agent_header = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_header = "Connection: close\r\n";
static const char *prox_header = "Proxy-Connection: close\r\n";
static const char *host_header_format = "Host: %s\r\n";
static const char *requestlint_header_format = "GET %s HTTP/1.0\r\n";
static const char *endof_header = "\r\n";


//헤더 키워드 상수들, 클라이언트로부터 받은 요청 헤더들을 읽을 때 사용
//프록시가 웹서버에 보낼 때 우리가 새로 만드는 고정된 값으로 대체하기 위한 키워드

static const char *connection_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";


//조건대로 헤더를 만들어서 tiny 서버에 넘겨줘야 한다. 
//그대로 보내주는 것 보다 정제해서 보내주는 방식이 더 안정적이다. 
void makeHTTPheader(char *http_header, char *hostname, char *path, int port, rio_t *client_rio)
{

  char buf[MAXLINE],request_header[MAXLINE],other_header[MAXLINE],host_header[MAXLINE];
  sprintf(request_header,requestlint_header_format,path);
  //tiny 서버에 보낼 요청 줄 생성하기 (프록시가 원하는 형식으로)
  // 예시 : GET /index.html HTTP/1.0\r\n
  while(Rio_readlineb(client_rio,buf,MAXLINE) > 0){
    //요청의 끝에 도달하기 전까지 계속 읽기
    //한줄씩 robust하게 읽는다.

    if(strcmp(buf,endof_header) == 0){ 

      // 스트링 비교하기 buf랑 endof_header 랑 동일한지 check

      break; // \r\n 줄이 오면 종료하라는 뜻의 구문 (헤더의 끝)
    }
    


    /*
    =============================================
    - Host :로 시작하는 헤더인지 확인
    - 맞다면 host_header에 따로 저장
    - 나중에 직접 구성된 헤더 조립할 때 포함
    =============================================
    
    ==============================================================
    int strncasecmp(const char *s1, const char *s2 , size_t n);

    -s1 : 비교할 첫 번째 문자열
    -s2 : 비교할 두 번째 문자열
    -n : 앞에서부터 최대 몇 글자까지 비교할지 길이 제한
    ==============================================================

    ==============================================================
    [proxy에서의 buf 역할]

    1.중간 저장소의 역할로 쓰인다.
    - 클라이언트의 요청을 읽을 때 

    ex) Rio_readlineb(&rio,buf,MAXLINE);

    -클라이언트가 보낸 요청 헤더 한 줄을 읽어서 buf에 저장 한다.
    -이후 buf의 내용을 분석하거나 전송한다. 

    2.서버 응답을 중계할 때 

    ex) while((n = Rio_readlineb(&server_rio,buf,MAXLINE)) > 0) {
      Rio_writen(clientfd,buf,n);
    } 

    -> buf는 네트워크 통신이나 파일 입출력에서 데이터를 임시로 저장하는
       문자열 배열이며, 요청이나 응답을 읽고, 분석하고 
       다시 보낼 때 꼭 필요한 중간 저장소이다. 
    ================================================================
    [other_header]란 ?
    ex ) char other_header[MAXLINE];

    -문자열 버퍼
    -클라이언트 요청 헤더 중에서 다음 네 가지 제외한 헤더를 저장

    */
    if(!strncasecmp(buf,host_key,strlen(host_key))){ 
      //대소문자 구분없이 문자열 비교하기 
      strcpy(host_header,buf); //복사해서 저장하기 
      continue;
    }
    if(!strncasecmp(buf,connection_key,strlen(connection_key)) && 
    !strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key)) &&
    !strncasecmp(buf,user_agent_key,strlen(user_agent_key))){
    continue; // 무시하고 넘어감
    }
    strcat(other_header,buf); 
    /*

    =================================================
    나머지 헤더만 추가해주기, 저 위에 3개 말고 !

    클라이언트가 보낸 요청 헤더 중에서 
    프록시가 직접 설정할 3가지 헤더는 무시하고,
    나머지 헤더만 모아서 "other_header"에 추가해주기
    ==================================================
    
    */
    

  }
  if(strlen(host_header) == 0)
  {
    sprintf(host_header,host_header_format,hostname);
  }
  //새로운 정제된 헤더 만들어 주기 !
  sprintf(http_header,"%s%s%s%s%s%s%s",
  request_header,
  host_header,
  conn_header,
  prox_header,
  user_agent_header,
  other_header,
  endof_header);
  
  
  return ; 


}

