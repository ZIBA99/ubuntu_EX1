#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define BUF_SIZE 1024

void read_childproc(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        printf("🔔 자식 프로세스 종료됨. 자원 회수 완료.\n");
    }
}

int main(int argc, char *argv[]) {
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t adr_sz;
    char buf[BUF_SIZE];
    pid_t pid;

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    // 자식 종료 시 시그널 처리
    signal(SIGCHLD, read_childproc);

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) {
        perror("socket error");
        exit(1);
    }

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1) {
        perror("bind error");
        exit(1);
    }

    if (listen(serv_sock, 5) == -1) {
        perror("listen error");
        exit(1);
    }

    printf("🚀 서버 실행 중... 포트: %s\n", argv[1]);

    while (1) {
        adr_sz = sizeof(clnt_adr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &adr_sz);
        if (clnt_sock == -1) continue;

        pid = fork();
        if (pid == -1) {
            close(clnt_sock);
            continue;
        }

        if (pid == 0) {  // 자식 프로세스
            close(serv_sock);
            while (1) {
                int str_len = read(clnt_sock, buf, BUF_SIZE);
                if (str_len == 0) {
                    printf("❌ 클라이언트 종료됨.\n");
                    break;
                }
                write(clnt_sock, buf, str_len);  // 에코
            }
            close(clnt_sock);
            return 0;
        } else {  // 부모 프로세스
            close(clnt_sock);  // 자식이 처리하므로 부모는 소켓 닫음
        }
    }

    close(serv_sock);
    return 0;
}

