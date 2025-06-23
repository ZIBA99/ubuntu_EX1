// chat_client.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8888
#define BUF_SIZE 1024

int sock;
void *recv_thread(void *arg) {
    char buf[BUF_SIZE];
    while (1) {
        int n = read(sock, buf, BUF_SIZE - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        printf("[메시지] %s", buf);
    }
    return NULL;
}

int main() {
    struct sockaddr_in serv_addr;
    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect 실패");
        exit(1);
    }

    printf("서버에 연결되었습니다. 메시지를 입력하세요\n");

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, NULL);

    char msg[BUF_SIZE];
    while (fgets(msg, BUF_SIZE, stdin) != NULL) {
        write(sock, msg, strlen(msg));
    }

    close(sock);
    return 0;
}
