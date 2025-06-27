#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>

#define MAX_CHILD 10
#define BUF_SIZE 1024

typedef struct {
    pid_t pid;
    int pipe_to_parent[2];  // 자식 → 부모
    int pipe_to_child[2];   // 부모 → 자식
    int client_fd;
    int active;
} ChildInfo;

ChildInfo children[MAX_CHILD];
int child_count = 0;

int serv_sock;

// SIGCHLD 처리 (좀비 제거)
void sigchld_handler(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

// SIGINT 처리 (Graceful 종료)
void sigint_handler(int sig) {
    printf("\n🛑 서버 종료 중...\n");
    for (int i = 0; i < child_count; i++) {
        if (children[i].active) {
            kill(children[i].pid, SIGTERM);
            close(children[i].pipe_to_child[1]);
            close(children[i].pipe_to_parent[0]);
            close(children[i].client_fd);
        }
    }
    close(serv_sock);
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_adr, clnt_adr;
    socklen_t adr_sz;
    char buf[BUF_SIZE];

    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);

    serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) perror("socket");

    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons(atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        perror("bind");

    if (listen(serv_sock, 5) == -1)
        perror("listen");

    printf("🚀 채팅 서버 실행 중... 포트: %s\n", argv[1]);

    while (1) {
        // 신규 클라이언트 accept
        adr_sz = sizeof(clnt_adr);
        int clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, &adr_sz);
        if (clnt_sock == -1) break;

        if (child_count >= MAX_CHILD) {
            printf("⚠️ 최대 접속자 수 초과\n");
            close(clnt_sock);
            continue;
        }

        int p2c[2], c2p[2];
        pipe(p2c);
        pipe(c2p);

        pid_t pid = fork();

        if (pid == 0) { // 자식
            close(serv_sock);
            close(p2c[1]);
            close(c2p[0]);

            while (1) {
                char msg[BUF_SIZE];
                int len = read(clnt_sock, msg, BUF_SIZE);
                if (len <= 0) break;

                write(c2p[1], msg, len);

                len = read(p2c[0], msg, BUF_SIZE);
                if (len > 0) write(clnt_sock, msg, len);
            }

            close(clnt_sock);
            close(p2c[0]);
            close(c2p[1]);
            exit(0);
        } else { // 부모
            close(p2c[0]);
            close(c2p[1]);

            children[child_count].pid = pid;
            children[child_count].pipe_to_parent[0] = c2p[0];
            children[child_count].pipe_to_child[1] = p2c[1];
            children[child_count].client_fd = clnt_sock;
            children[child_count].active = 1;
            child_count++;
        }

        // 브로드캐스트 처리 루프
        fd_set reads;
        FD_ZERO(&reads);
        int maxfd = 0;
        for (int i = 0; i < child_count; i++) {
            if (!children[i].active) continue;
            FD_SET(children[i].pipe_to_parent[0], &reads);
            if (children[i].pipe_to_parent[0] > maxfd)
                maxfd = children[i].pipe_to_parent[0];
        }

        struct timeval timeout = {0, 500000}; // 0.5초
        int ret = select(maxfd + 1, &reads, NULL, NULL, &timeout);

        if (ret > 0) {
            for (int i = 0; i < child_count; i++) {
                if (!children[i].active) continue;
                if (FD_ISSET(children[i].pipe_to_parent[0], &reads)) {
                    int len = read(children[i].pipe_to_parent[0], buf, BUF_SIZE);
                    if (len > 0) {
                        printf("📨 [%d번 자식] %.*s", i, len, buf);
                        for (int j = 0; j < child_count; j++) {
                            if (i == j || !children[j].active) continue;
                            write(children[j].pipe_to_child[1], buf, len);
                        }
                    }
                }
            }
        }
    }

    return 0;
}
