#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);  // 로컬 호스트

    connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));

    printf("서버에 보낼 메시지 입력: ");
    fgets(buffer, BUFFER_SIZE, stdin);

    write(sockfd, buffer, strlen(buffer));

    int n = read(sockfd, buffer, BUFFER_SIZE);
    buffer[n] = '\0';
    printf("서버 응답: %s\n", buffer);

    close(sockfd);
    return 0;
}

