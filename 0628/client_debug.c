#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// 현재 시간을 포함한 로그 출력 함수
void log_debug(const char *format, ...) {
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));
    
    va_list args;
    va_start(args, format);
    
    printf("[DEBUG %s] ", time_str);
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    char message[BUFFER_SIZE] = "안녕하세요, 서버!";
    
    // 소켓 생성 디버깅
    log_debug("소켓 생성 시도...");
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        log_debug("소켓 생성 실패");
        perror("소켓 생성 오류");
        return -1;
    }
    log_debug("소켓 생성 성공 (소켓 디스크립터: %d)", sock);
    
    // 서버 주소 설정 디버깅
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // IP 주소 변환 디버깅
    log_debug("서버 IP 주소 변환 시도 (IP: %s)...", argc > 1 ? argv[1] : "127.0.0.1");
    if (inet_pton(AF_INET, argc > 1 ? argv[1] : "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        log_debug("IP 주소 변환 실패: %s", argc > 1 ? argv[1] : "127.0.0.1");
        perror("주소 변환 오류");
        return -1;
    }
    log_debug("IP 주소 변환 성공");
    
    // 연결 시도 디버깅
    log_debug("서버 연결 시도 (IP: %s, 포트: %d)...", 
              inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        log_debug("서버 연결 실패 (IP: %s, 포트: %d)", 
                 inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));
        perror("연결 오류");
        return -1;
    }
    log_debug("서버 연결 성공!");
    
    // 데이터 전송 디버깅
    log_debug("전송 데이터 준비: '%s' (%ld 바이트)", message, strlen(message));
    log_debug("데이터 전송 시도...");
    
    int bytes_sent = send(sock, message, strlen(message), 0);
    if (bytes_sent < 0) {
        log_debug("데이터 전송 실패");
        perror("전송 오류");
        return -1;
    }
    log_debug("데이터 전송 성공: %d 바이트 전송됨", bytes_sent);
    
    // 응답 수신 디버깅
    log_debug("서버 응답 대기 중...");
    int bytes_received = read(sock, buffer, BUFFER_SIZE);
    if (bytes_received < 0) {
        log_debug("응답 수신 실패");
        perror("수신 오류");
        return -1;
    }
    log_debug("응답 수신 성공: %d 바이트 수신됨", bytes_received);
    log_debug("수신 데이터: '%s'", buffer);
    
    // 소켓 종료 디버깅
    log_debug("소켓 종료 중...");
    close(sock);
    log_debug("소켓 종료 완료");
    
    return 0;
}
