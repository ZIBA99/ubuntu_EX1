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
#include <time.h>

#define NAME_LEN 20

#define PORT 8888
#define BUF_SIZE 1024
#define MAX_CLIENTS 10

typedef struct {
    pid_t pid;
    int p2c_write;
    int c2p_read;
    char nickname[NAME_LEN];
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
FILE *log_fp;

void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void log_message(const char *msg) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_fp, "[%s] %s", time_buf, msg);
    fflush(log_fp);
}

void handle_client(int sock, int pipe_read, int pipe_write) {
    char buf[BUF_SIZE];
    int n;

    // 1. 닉네임 수신
    write(sock, "Enter your nickname: ", strlen("Enter your nickname: "));
    n = read(sock, buf, NAME_LEN);
    buf[n - 1] = '\0';
    write(pipe_write, buf, strlen(buf)); // 첫 번째 메시지는 닉네임

    while (1) {
        // 메시지 클라이언트 → 부모
        n = read(sock, buf, BUF_SIZE);
        if (n <= 0) break;
        write(pipe_write, buf, n);

        // 메시지 부모 → 클라이언트
        n = read(pipe_read, buf, BUF_SIZE);
        if (n <= 0) break;
        write(sock, buf, n);
    }

    close(sock);
    close(pipe_read);
    close(pipe_write);
    exit(0);
}

void broadcast(int sender_index, const char *msg, int len) {
    for (int i = 0; i < client_count; i++) {
        if (i == sender_index) continue;
        write(clients[i].p2c_write, msg, len);
    }
    log_message(msg);
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    signal(SIGCHLD, sigchld_handler);
    log_fp = fopen("chat_log.txt", "a");

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    listen(serv_sock, 5);
    printf("[서버 실행 중] 포트: %d\n", PORT);

    while (1) {
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
                set_nonblocking(c2p[0]);

                // 첫 메시지로 닉네임 받기
                char nick[NAME_LEN] = {0};
                read(c2p[0], nick, NAME_LEN);

                snprintf(clients[client_count].nickname, NAME_LEN, "%s", nick);
                clients[client_count].pid = pid;
                clients[client_count].p2c_write = p2c[1];
                clients[client_count].c2p_read = c2p[0];

                // 접속 알림
                char join_msg[BUF_SIZE];
                snprintf(join_msg, sizeof(join_msg), "[알림] %s 님이 입장했습니다.\n", nick);
                broadcast(-1, join_msg, strlen(join_msg));

                client_count++;
            }
        }

        // 메시지 수신 + 브로드캐스트 루프
        char buf[BUF_SIZE];
        for (int i = 0; i < client_count; i++) {
            int n = read(clients[i].c2p_read, buf, BUF_SIZE);
            if (n > 0) {
                char msg[BUF_SIZE + NAME_LEN];
                snprintf(msg, sizeof(msg), "%s: %.*s", clients[i].nickname, n, buf);
                broadcast(i, msg, strlen(msg));
            }
        }

        usleep(10000);
    }

    fclose(log_fp);
    close(serv_sock);
    return 0;
}

