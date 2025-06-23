#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>

#define BUF_SIZE 100
#define MAX_CLNT 256

void error_handling(char *message);
void handle_client(int clnt_sock);
void read_childproc(int sig);

int main(int argc, char *argv[])
{
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t adr_sz;
    pid_t pid;
    struct sigaction act;
    
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    
    // 시그널 핸들러 등록 (좀비 프로세스 방지)
    act.sa_handler = read_childproc;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGCHLD, &act, 0);
    
    // 서버 소켓 생성
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
        error_handling("socket() error");
    
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));
    
    // 소켓에 주소 할당
    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        error_handling("bind() error");
    
    // 연결 요청 대기 상태로 진입
    if (listen(serv_sock, 5) == -1)
        error_handling("listen() error");
    
    printf("채팅 서버가 시작되었습니다. (포트: %s)\n", argv[1]);
    
    while (1) {
        adr_sz = sizeof(clnt_adr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &adr_sz);
        if (clnt_sock == -1)
            continue;
        
        printf("새로운 클라이언트가 연결되었습니다: %s\n", 
               inet_ntoa(clnt_adr.sin_addr));
        
        // 클라이언트 연결마다 새로운 프로세스 생성
        pid = fork();
        if (pid == -1) {
            close(clnt_sock);
            continue;
        }
        
        if (pid == 0) {  // 자식 프로세스
            close(serv_sock);  // 자식 프로세스는 서버 소켓 필요 없음
            handle_client(clnt_sock);
            close(clnt_sock);
            exit(0);
        } else {  // 부모 프로세스
            close(clnt_sock);  // 부모 프로세스는 클라이언트 소켓 필요 없음
        }
    }
    
    close(serv_sock);
    return 0;
}

void handle_client(int clnt_sock)
{
    char buf[BUF_SIZE];
    int str_len;
    
    // 클라이언트 ID 수신
    str_len = read(clnt_sock, buf, BUF_SIZE);
    buf[str_len] = 0;
    printf("클라이언트 [%s] 접속\n", buf);
    
    // 클라이언트와 메시지 교환
    while ((str_len = read(clnt_sock, buf, BUF_SIZE)) != 0) {
        buf[str_len] = 0;
        printf("클라이언트 메시지: %s\n", buf);
        
        // 에코 서비스 제공
        write(clnt_sock, buf, str_len);
    }
    
    printf("클라이언트 연결 종료\n");
}

void read_childproc(int sig)
{
    pid_t pid;
    int status;
    
    // 종료된 자식 프로세스 처리 (좀비 프로세스 방지)
    pid = waitpid(-1, &status, WNOHANG);
    printf("종료된 프로세스 ID: %d\n", pid);
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
