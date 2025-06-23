// chat_server_fork_noselect.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define PORT 8888
#define BUF_SIZE 1024
#define MAX_CLIENTS 10

typedef struct {
    pid_t pid;
    int p2c_write;  // 부모 → 자식 파이프 (쓰기)
    int c2p_read;   // 자식 → 부모 파이프 (읽기)
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;

void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// 자식: 클라이언트 ↔ 자식 ↔ 부모
void handle_client(int sock, int pipe_read, int pipe_write) {
    char buf[BUF_SIZE];
    int n;

    while (1) {
        // 클라이언트로부터 입력 받기
        n = read(sock, buf, BUF_SIZE);
        if (n <= 0) break;
        write(pipe_write, buf, n);  // 부모로 전송

        // 부모로부터 메시지 받기
        n = read(pipe_read, buf, BUF_SIZE);
        if (n <= 0) break;
        write(sock, buf, n);  // 클라이언트에게 전송
    }

    close(sock);
    close(pipe_read);
    close(pipe_write);
    exit(0);
}

// 부모가 메시지를 다른 자식에게 전송
void broadcast(int sender_index, char *msg, int len) {
    for (int i = 0; i < client_count; i++) {
        if (i == sender_index) continue;
        write(clients[i].p2c_write, msg, len);
    }
}

// 파이프를 논블로킹으로 변경
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    signal(SIGCHLD, sigchld_handler);

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    listen(serv_sock, 5);

    printf("서버 실행 중...\n");

    while (1) {
        // 1. 클라이언트 수락 (논블로킹 처리)
        struct sockaddr_in clnt_addr;
        socklen_t clnt_size = sizeof(clnt_addr);
        int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_size);

        if (clnt_sock >= 0) {
            if (client_count >= MAX_CLIENTS) {
                close(clnt_sock);
                continue;
            }

            int c2p[2], p2c[2];
            pipe(c2p); pipe(p2c);

            pid_t pid = fork();
            if (pid == 0) {
                close(serv_sock);
                close(c2p[0]); close(p2c[1]);
                handle_client(clnt_sock, p2c[0], c2p[1]);
            } else {
                close(clnt_sock);
                close(c2p[1]); close(p2c[0]);
                set_nonblocking(c2p[0]);  // 부모 읽기 pipe 비블로킹

                clients[client_count].pid = pid;
                clients[client_count].p2c_write = p2c[1];
                clients[client_count].c2p_read = c2p[0];
                client_count++;
            }
        }

        // 2. 각 자식 pipe 읽고, 브로드캐스트
        char buf[BUF_SIZE];
        for (int i = 0; i < client_count; i++) {
            int n = read(clients[i].c2p_read, buf, BUF_SIZE);
            if (n > 0) {
                broadcast(i, buf, n);
            }
        }

        usleep(10000);  // CPU 점유율 방지
    }

    close(serv_sock);
    return 0;
}

