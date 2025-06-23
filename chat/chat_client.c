// chat_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 1024
#define NICKNAME_SIZE 32

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char nickname[NICKNAME_SIZE];
    pid_t pid;

    printf("닉네임 입력: ");
    fgets(nickname, NICKNAME_SIZE, stdin);
    nickname[strcspn(nickname, "\n")] = '\0';

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("소켓 생성 실패");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("서버 연결 실패");
        exit(1);
    }

    // 닉네임 전송
    write(sock, nickname, strlen(nickname));

    pid = fork();
    if (pid == 0) {
        // 사용자 입력 → 서버 전송
        while (fgets(buffer, BUFFER_SIZE, stdin)) {
            write(sock, buffer, strlen(buffer));
        }
    } else {
        // 서버 → 출력
        while (1) {
            int n = read(sock, buffer, BUFFER_SIZE - 1);
            if (n <= 0) break;
            buffer[n] = '\0';
            printf("%s", buffer);
        }
    }

    close(sock);
    return 0;
}
