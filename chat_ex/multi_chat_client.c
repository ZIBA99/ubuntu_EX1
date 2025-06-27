#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in serv_addr;
    char msg[BUF_SIZE], recv_msg[BUF_SIZE];

    if (argc != 3) {
        printf("Usage: %s <IP> <Port>\n", argv[0]);
        return 1;
    }

    sock = socket(PF_INET, SOCK_STREAM, 0);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect");
        return 1;
    }

    printf("✅ 채팅 서버에 접속했습니다. 메시지를 입력하세요\n");

    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(0, &read_fds);
        FD_SET(sock, &read_fds);

        select(sock+1, &read_fds, NULL, NULL, NULL);

        if (FD_ISSET(0, &read_fds)) {
            fgets(msg, BUF_SIZE, stdin);
            if (!strcmp(msg, "q\n")) break;
            write(sock, msg, strlen(msg));
        }

        if (FD_ISSET(sock, &read_fds)) {
            int len = read(sock, recv_msg, BUF_SIZE);
            if (len <= 0) break;
            recv_msg[len] = '\0';
            printf("서버: %s", recv_msg);
        }
    }

    close(sock);
    return 0;
}
