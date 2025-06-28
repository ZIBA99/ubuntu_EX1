#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345
#define BUFFER_SIZE 1024

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);

    printf("서버 대기 중...\n");

    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    printf("클라이언트 연결됨\n");

    int n = read(client_fd, buffer, BUFFER_SIZE);
    buffer[n] = '\0';
    printf("클라이언트로부터 수신: %s\n", buffer);

    char *reply = "수신 완료";
    write(client_fd, reply, strlen(reply));

    close(client_fd);
    close(server_fd);
    return 0;
}

