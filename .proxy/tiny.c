#include <signal.h>
#include <locale.h>
#include "csapp.h"



void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "ko_KR.utf8");
    /*

    서버 시작점
    포트 번호 인자로 받고,listen 소켓 열고 무한 루프
    accept로 연결 수락하고, doit 호출하기
    종료 후 close()로 소켓 닫는다. 

    int argc (argument count) : 인자의 개수
    int argv (argument vector) : 인자의 내용들을 담은 배열

    */

    int listenfd, connfd ;
    char hostname[MAXLINE] , port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; 

    //인자 갯수 확인 해주기
    if(argc != 2){
        fprintf(stderr, "usage : %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    //서버에서 listen하기
    while(1) {
        //true일 동안 무한루프 실행하기 
        clientlen = sizeof(clientaddr);
        /*
        clientaddr는 클라이언트의 IP 주소 정보가 저장될 구조체
        accept는 이걸 입출력 파라미터로 사용한다
        => 주소를 채워주고, 실제 크기도 여기에 쓴다. 

        [요약]
        clientaddr의 크기를 clientlen에
        미리 넣어서 accept준비
        */

        connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);
        //클라이언트의 연결 요청을 수락해서 , 통신용 소켓 생성
        Getnameinfo((SA *) &clientaddr , clientlen, hostname , MAXLINE, port, MAXLINE, 0);
        /*
        클라이언트의 IP 주소와 포트 번호를 문자열로 변환

        tiny 웹서버에서 클라이언트 연결을 받아들이고
        클라이언트 주소를 얻는 핵심 코드이다. 
        */
        
        printf("(%s, %s) 로 부터의 연결 요청 수락\n",hostname,port);
        doit(connfd);
        Close(connfd);
    }



}

void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE] , version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    //1. 요청 라인과 헤더를 읽는다.

    Rio_readinitb(&rio,fd);
    Rio_readlineb(&rio,buf,MAXLINE);
    printf("Ruquest headers:\n");
    printf("%s",buf);
    sscanf(buf,"%s %s %s",method,uri,version);
    //printf("Requested URI: %s\n", uri); 오류 검출하려고 작성했었음

    if (strcasecmp(method,"GET")){
        //GET 요청 맞는지 확인해주기 같으면 0 반환 에러면 1 반환
        //근데 1이 true니까 1이면 아래의 구문이 실행되는거임 if (true(1))
        clienterror(fd,method,"501","처리 불가","Tiny가 처리할 수 없는 요청입니다.");
        //이 에러처리 부분 proxy에는 없음
        return;
    }
    read_requesthdrs(&rio); // 다른 요청은 무시 
    /*

    [예시]

    GET /index.html HTTP/1.1\r\n
    Host: localhost:8000\r\n
    User-Agent: Mozilla/5.0\r\n
    Accept: 
    
    이런식으로 요청이 들어온다면 맨 윗줄만 분석해주고 
    그 아랫줄의 부가적인 정보들은 이 함수가 읽고 무시해준다. 
    */

    //2. parse URI로 부터 GET 요청 받기

    /*
    이 부분 
    */

    is_static = parse_uri(uri,filename,cgiargs);
    //리턴 값 1이면 정적콘텐츠, 0 이면 동적콘텐츠
    if(stat(filename,&sbuf) < 0 ){
        //해당 파일이 존재하지 않으면 stat()은 -1 반환한다. 
        clienterror(fd,filename,"404","Not found","Tiny가 해당 파일을 찾을 수 없습니다."); 
        return; 
    }

    //정적 콘텐츠 일 때 처리 해주기
    if(is_static){ //is_static이 1 일때 (true 일 때 = 정적 콘텐츠 일 때)
       if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
        clienterror(fd,filename,"403","Forbidden","Tiny가 해당 파일을 읽을 수 없습니다.");
        return;
       }
       serve_static(fd,filename,sbuf.st_size);
    }
    //동적 콘텐츠 일 때 처리 해주기
    else{
        if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
            clienterror(fd,filename,"403","Forbidden","Tiny가 해당 파일을 읽을 수 없습니다.");

            return;
        }
        serve_dynamic(fd,filename,cgiargs);

    }

    /*

    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
    clienterror(fd, filename, "403", "Forbidden", "Tiny가 해당 파일을 읽을 수 없습니다.");
    return;
    }
     
    -정적 또는 동적 콘텐츠 요청을 받았을 때 , 그 파일을 '실제로 읽을 수 있는지'
    -보안 검사 ! 

    [이 코드는 정적이든 동적이든 공통적으로]
    
    1.해당 경로의 파일이 실제 "정상적인 파일" 인지 확인
    2.서버가 그 파일을 "읽을 수 있는 권한"이 있는지 확인
    3.둘 중 하나라도 아니라면 -> 403 forbidden 에러 전송

    S_IRUSR: "소유자가 읽을 수 있는" 권한 비트 (Linux 권한 중 하나)

    & sbuf.st_mode: 실제 이 파일이 그런 권한을 갖고 있는지 검사
    
    */

}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    /*
    클라이언트 요청이 잘못되었거나 처리 불가능할 때
    HTTP 응답 + 간단한 HTML 에러 메세지를 브라우저에 전송하는 함수

    에러상황을 html 문서 형식으로 만들어서 브라우저 전송

    */
     //sprintf = 출력 X, 우측을 좌측 버퍼에 덮어쓰기 방식으로 저장, 앞에 매번 body를 두는것도 그 때문문
  
  char buf[MAXLINE], body[MAXBUF];
  
  sprintf(body, "<html><title>Tiny error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s : %s\r\n",body, errnum,shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  // 이건 출력 함수임 
  /*
  fd :  클라이언트 소켓 
  buf or body : 버퍼에 저장된 응답 내용
  write() : 시스템콜을 통해 클라이언트에게 전송
  */
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));

   
}

void read_requesthdrs(rio_t *rp)

{

    //http 요청의 헤더 부분을 읽어서 무시하는 함수
    char buf[MAXLINE];

    Rio_readlineb(rp,buf,MAXLINE);
    while(strcmp(buf,"\r\n")){
        //첫번째 줄만 읽고 나머지는 skip
        Rio_readlineb(rp,buf,MAXLINE);
        printf("%s",buf);
    }

    return;

}


int parse_uri(char *uri, char *filename, char *cgiargs)
{
    //http URI을 분석하는 함수 !
    char *ptr;
    /*
    1.strstr() : 문자열 안에서 특정 "부분 문자열"이 
    처음 나오는 위치를 찾아주는 함수

    [반환값]
    포인터 or NULL

    2.strcpy(dst,src) : src문자열을 dst로 복사
    
    [반환값]
    dst 포인터

    3.strcat(dst,src) : dst끝에 src를 이어붙인다. 

    [반환값]
    dst 포인터 

    [반환값] 
    -부분 문자열이 발견되면 => 해당 위치의 포인터 반환
    -없으면 => NULL 반환
    
    */

    /*
    이 함수는 클라이언트가 요청한 URI를 기반으로 실제 파일 경로를 생성
    정적 콘텐츠 처리에서 아주 중요한 역할


    
    */

    if(!strstr(uri,"cgi-bin")) { // URI에 이 문자열이 없으면 정적 컨텐츠
       strcpy(cgiargs,"");
       strcpy(filename,".");
       strcat(filename,uri); //여기서 오타났어서 안됨 (원준석님 발견 ㄷ ㄷ) 
       if (uri[strlen(uri)-1] == '/') //디렉터리 요청인 경우는 기본파일 주기
          strcat(filename,"home.html"); //filename의 끝에 home.html을 붙인다. 
        //현재 디렉토리 기준의 파일 경로로 바뀐다.
        return 1;
    }
    else{  // 동적 컨텐츠 일 때
        ptr = index(uri,'?');
        if(ptr){
            strcpy(cgiargs,ptr+1);
            *ptr='\0';
        }
        else
            strcpy(cgiargs,"");
        strcpy(filename,".");
        strcat(filename,uri); 
        //filename 끝에 uri를 이어붙인다. 
        return 0;
        
    }
   
}

void serve_static(int fd, char *filename, int filesize)
{
    /*
    -정적 콘텐츠를 클라이언트에 보내준다.
    */
    int srcfd; 
    /*
    정적파일을 열었을 때 반환되는 파일 디스크립터(해석), 파일 식별자
    이걸로 mmap()에 매핑하거나 close()에 사용한다. 
    */
    char *srcp, filetype[MAXLINE], buf[MAXBUF];
    /*

    1.[*srcp] 

    파일 내용을 메모리에 매핑한 주소(포인터)
    scrp = Mmap(. . .) 결과
    파일 전체가 이 포인터를 통해 메모리처럼 접근 가능
    이후 Rio_written에서 사용된다.
    
    2.char filetype[MAXLINE];

    -클라이언트에게 보낼 HTTP 응답의 MIME 타입을 저장하는 문자열
    -클라이언트에게 파일 형식을 알려주는 Content-type 헤더에 사용

    3.char buf[MAXBUF];

    -HTTP 응답 헤더를 조립할 때 사용하는 문자열 버퍼
    -HTTP 헤더 한 덩어리를 저장할 임시 공간 

    */

    //응답 헤더를 클라이언트측에 보낸다.
    get_filetype(filename,filetype); // 접미어부분을 검사해서 파일타입 결정
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);
    sprintf(buf, "%sConnection: close\r\n",buf);
    sprintf(buf, "%sContent-length: %d\r\n",buf,filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n",buf,filetype);
    Rio_writen(fd,buf,strlen(buf));
    printf("Response headers:\n");
    printf("%s",buf);


    //웅답 body를 클라이언트에 보낸다.
    srcfd = Open(filename,O_RDONLY,0); //파일 접근 모드 플래그
    srcp = Mmap(0,filesize,PROT_READ,MAP_PRIVATE,srcfd,0);
    Close(srcfd);
    Rio_writen(fd,srcp,filesize);
    Munmap(srcp,filesize); //mmap로 매핑한 메모리 영역 해제하는 시스템콜
    


    
}


void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    //자식 프로세스를 fork() 한 후, 자식 컨텍스트에서 실행

    char buf[MAXLINE] , *emptylist[] = { NULL };
    
    //1.http의 응답의 첫 부분을 반환 한다. 
    sprintf(buf,"HTTP/1.0 200 OK\r\n"); //버퍼 메모리에 쓴다.
    Rio_writen(fd,buf,strlen(buf));
    sprintf(buf,"Server : Tiny Web Server\r\n");
    Rio_writen(fd,buf,strlen(buf));

    if (Fork() == 0){ //자식 프로세스 생성해주기
    /*
    -CGI 프로그램 실행은 자식 프로세스에서만 이루어져야 한다.
    -부모는 기다리고, 자식은 실행한다.

    => 안전성과 '독립성' 확보 목적
    
    */
       setenv("QUERY_STRING",cgiargs,1); //환경 변수 설정 해주기
       Dup2(fd,STDOUT_FILENO);
       //클라이언트에게 데이터를 전달해주는 경로 설정 함수
       //CGI의 출력이 클라이언트로 바로 전송됨
       Execve(filename,emptylist,environ); //RUN CGI program
       /*
       environ은 환경 변수, 시스템 환경 정보 전달 한다. 
       */
    }

    Wait(NULL); // 자식 프로세스가 종료될 때까지 기다린다. 

}
    


void get_filetype(char *filename, char *filetype)
{
   // 파일이름으로 부터 파일타입 파생시키기 5개의 형식
   if(strstr(filename,".html"))
      strcpy(filetype,"text/html");
   else if(strstr(filename,".gif"))
      strcpy(filetype,"image/gif");
   else if(strstr(filename,".png"))
      strcpy(filetype,"image/png");
   else if(strstr(filename,".jpeg"))
      strcpy(filetype,"image/jpeg");
   else
      strcpy(filetype,"text/plain");  
}

