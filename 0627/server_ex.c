#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345
#define BACKLOG 5
#define BUFSIZE 1024

int main() {
    int serv_sock, clnt_sock;
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    char buf[BUFSIZE];

    // 1. 서버 소켓 생성
    serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock < 0) {
        perror("socket");
        exit(1);
    }

    // 2. 주소 정보 초기화 및 바인드
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // 모든 인터페이스 바인드
    serv_addr.sin_port = htons(PORT);

    if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(serv_sock);
        exit(1);
    }

    // 3. 클라이언트 연결 요청 대기
    if (listen(serv_sock, BACKLOG) < 0) {
        perror("listen");
        close(serv_sock);
        exit(1);
    }

    printf("서버가 포트 %d 에서 대기 중...\n", PORT);

    clnt_addr_size = sizeof(clnt_addr);

    // 4. 클라이언트 연결 수락
    clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
    if (clnt_sock < 0) {
        perror("accept");
        close(serv_sock);
        exit(1);
    }

    printf("클라이언트 연결됨\n");

    // 5. 클라이언트 메시지 수신 및 에코
    while (1) {
        ssize_t n = read(clnt_sock, buf, BUFSIZE - 1);
        if (n <= 0) {
            printf("클라이언트 연결 종료\n");
            break;
        }
        buf[n] = '\0';
        printf("받은 메시지: %s", buf);

        // 그대로 클라이언트에게 다시 보내기
        write(clnt_sock, buf, n);
    }

    // 6. 소켓 닫기
    close(clnt_sock);
    close(serv_sock);

    return 0;
}
