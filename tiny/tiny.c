/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"
#include <stdlib.h>
#define original_staticx

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* doit에서 쓰이는 stat struct */
// struct stat{
//   dev_t st_dev; /* ID of device containing file */
// ino_t st_ino; /* inode number */
// mode_t st_mode; /* 파일의 종류 및 접근권한 */
// nlink_t st_nlink; /* hardlink 된 횟수 */
// uid_t st_uid; /* 파일의 owner */
// gid_t st_gid; /* group ID of owner */
// dev_t st_rdev; /* device ID (if special file) */
// off_t st_size; /* 파일의 크기(bytes) */
// blksize_t st_blksize; /* blocksize for file system I/O */
// blkcnt_t st_blocks; /* number of 512B blocks allocated */
// time_t st_atime; /* time of last access */
// time_t st_mtime; /* time of last modification */
// time_t st_ctime; /* time of last status change */ 
// }

// 한개의 HTTP 트랜잭션을 처리함.
void doit(int fd) // fd = file descriptor = 파일 식별자
{
  // 정적파일인지 아닌지를 판단해주기 위한 변수
  int is_static;
  // 파일에 대한 정보를 가지고 있는 구조체
  struct stat sbuf;
  // HTTP request를 받아들이기 위한 버퍼 및 각각의 요청 메서드, URI, HTTP version을 저장
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  // 각각 static / dynamic content의 파일명 및 CGI인자들을 저장.
  // 'filenam'은 HTTP request에서 추출한 파일의 이름을 저장
  // 'cgiargs'는 HTTP request에서 추출한 CGI 인자들을 저장
  char filename[MAXLINE], cgiargs[MAXLINE];
  // rio 라이브러리를 사용하기 위한 구조체. rio 라이브러리는 표준 C 라이브러리의 I/O함수를 래핑하여, 더욱 안정적인 I/O를 제공.
  rio_t rio;

  /* Read request line and headers */
  // rio(robust I/O (Rio)) 초기화
  Rio_readinitb(&rio, fd);
  // buf에서 client request 읽어들이기
  // rio_readlineb함수를 사용해서 요청 라인을 읽어들임
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  // request header 출력
  printf("%s", buf);
  // buf에 있는 데이터를 method, uri, version에 담기
  sscanf(buf, "%s %s %s", method, uri, version);

  // method가 GET이 아니라면 error message 출력 + main 루틴으로 회귀
  // 그 후에 연결을 닫고 다음 요청을 기다림
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  // 다른 요청 헤더들을 무시한다.
  read_requesthdrs(&rio);

  /* URI를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그 */
  is_static = parse_uri(uri, filename, cgiargs);
  // filename에 맞는 정보 조회를 하지 못했으면 error message 출력 (return 0 if success else -1)
  if (stat(filename, &sbuf) < 0) {
      clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
      return;
  }
  // request file이 static contents이면 실행
  if (is_static) {
    // file이 정규파일이 아니거나 사용자 읽기가 안되면 error message 출력
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {// 이 파일이 보통 파일 이라는 것과 읽기 권한을 가지고 있는지를 검증
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    // 위 검증에서 정적파일이라는게 정적파일을 클라이언트에게 제공
    serve_static(fd, filename, sbuf.st_size, method);
  }
  // request file이 dynamic contents이면 실행
  else {
    // file이 정규파일이 아니거나 사용자 읽기가 안되면 error message 출력
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
      "Tiny couldn't run the CGI program");
      return;
  }
  // 위 검증을 거치면 동적 컨텐츠를 클라이언트에게 제공
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

// error 발생 시, client에게 보내기 위한 response (error message)
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  // response body 쓰기 (HTML 형식)
  // 브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML 파일도 함께 보냄
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  //response 쓰기
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);        // 버전, 에러번호, 상태메시지
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));                          // body 입력
}

// request header를 읽기 위한 함수
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  // 빈 텍스트 줄이 아닐 때까지 읽기
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

// uri parsing을 하여 static을 request하면 0, dynamic을 request하면 1반환
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // parsing 결과, static file request인 경우 (uri에 cgi-bin이 포함이 되어 있지 않으면)
  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    // request에서 특정 static contents를 요구하지 않은 경우 (home page 반환)
    if (uri[strlen(uri)-1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  }
  // parsing 결과, dynamic file request인 경우
  else {
    // uri부분에서 file name과 args를 구분하는 ?위치 찾기
    ptr = index(uri, '?');
    // ?가 있으면
    if (ptr) {
      //cgiargs에 인자 넣어주기
      strcpy(cgiargs, ptr+1);
      // 포인터 ptr은 null처리
      *ptr = '\0';
    }
    // ?가 없으면
    else {
      strcpy(cgiargs, "");
    }
    // filename에 uri담기
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

#ifdef original_static
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

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

#else
// HEAD method 처리를 위한 인자 추가
void serve_static(int fd, char *filename, int filesize, char *method)
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

  if(strcasecmp(method, "GET") == 0) {
    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
    // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    // solved problem 11.9
    srcp = malloc(filesize);
    Rio_readn(srcfd, srcp, filesize);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    // Munmap(srcp, filesize);
    free(srcp);
  }
}
#endif
  /* * get_filetype - Derive file type from filename*/
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
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) { /* Child */
  /* Real server would set all CGI vars here */
  setenv("QUERY_STRING", cgiargs, 1);
  // method를 cgi-bin/adder.c에 넘겨주기 위해 환경변수 set
  setenv("REQUEST_METHOD", method, 1);
  Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
  Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}


// argc(argument count): 인자 개수(올바른 인자개수는 2개 --> 실행할 프로그램 , 포트번호) , argv: par 값
// Ex ) ./tiny 8080 면 argc는 2 , argv는 tiny와 8080 두 개 들어감
int main(int argc, char **argv)             
{
  /*
    argv[0] : 프로그램의 이름
    argv[1] : 포트번호
  */

  // listenfd : 들어오는 연결 요청을 받기 위한 소켓 식별자
  // connfd : 연결된 클라이언트 소켓 식별자
  int listenfd, connfd;
  // hostname : 'getnameinfo' 함수에서 클라이언트 소켓의 IP주소를 저장하기 위한 문자열 변수.
  // port : 'getnameinfo' 함수에서 클라이언트 소켓의 포트 번호를 저장하기 위한 문자열 변수.
  char hostname[MAXLINE], port[MAXLINE];
 
  // accept로 보내지는 소켓 주소 구조체의 크기정보
  socklen_t clientlen;
  // accept로 보내지는 소켓 주소 구조체
  struct sockaddr_storage clientaddr;

  // 입력 인자가 2개가 아니면
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 듣기 소켓 오픈 ! 
  listenfd = open_listenfd(argv[1]);
  // 무한 서버 루프 실행
  while (1) {
    clientlen = sizeof(clientaddr);
    /*
      서버는 accept 함수를 호출해서 클라이언트로의 연결 요청을 기다린다.
      client 소켓은 server 소켓의 주소를 알고 있으니까
      client에서 server로 넘어올 때 add정보를 가지고 올 것이라고 가정
      accept 함수는 연결되면 식별자 connfd를 return.
    */
    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);                       // SA: type of struct socketaddr
    // client socket에서 hostname과 port number를 스트링으로 변환
    getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd); // 트랜잭션 수행 즉, 클라이언트가 요청한 작업을 수행하고, 클라이언트에게 응답을 보내는 작업을 수행합니다.
    close(connfd); // 클라이언트와의 연결을 종료하는 작업을 수행
  }
}