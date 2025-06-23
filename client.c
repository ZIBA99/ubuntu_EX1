//chat_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <termios.h>

#define BUFFER_SIZE 1024

int socket_fd;
int running = 1;
pid_t child_pid;

// 서버로부터 메시지 수신 처리 함수
void receive_messages() {
    char buffer[BUFFER_SIZE];
    int bytes;
    
    while (running) {
        bytes = read(socket_fd, buffer, BUFFER_SIZE - 1);
        if (bytes <= 0) {
            printf("서버와의 연결이 종료되었습니다.\n");
            running = 0;
            kill(getppid(), SIGTERM);  // 부모 프로세스에게 종료 신호 전송
            break;
        }
        
        buffer[bytes] = '\0';
        printf("%s\n", buffer);
    }
    
    exit(0);
}

// 시그널 핸들러 함수
void handle_signal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        running = 0;
        if (child_pid > 0) {
            kill(child_pid, SIGTERM);  // 자식 프로세스 종료
        }
        close(socket_fd);
        printf("\n프로그램을 종료합니다.\n");
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    
    // 명령행 인자 확인
    if (argc != 3) {
        printf("사용법: %s <서버 주소> <포트>\n", argv[0]);
        exit(1);
    }
    
    // 시그널 핸들러 설정
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    
    // 소켓 생성
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("소켓 생성 실패");
        exit(1);
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    
    // 서버 주소 변환
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("주소 변환 실패");
        exit(1);
    }
    
    // 서버에 연결
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("연결 실패");
        exit(1);
    }
    
    printf("서버에 연결되었습니다.\n");
    
    // 자식 프로세스 생성 (메시지 수신용)
    child_pid = fork();
    
    if (child_pid < 0) {
        perror("프로세스 생성 실패");
        close(socket_fd);
        exit(1);
    } else if (child_pid == 0) {
        // 자식 프로세스 - 메시지 수신 담당
        receive_messages();
    } else {
        // 부모 프로세스 - 메시지 전송 담당
        while (running) {
            // 사용자 입력 받기
            fgets(buffer, BUFFER_SIZE, stdin);
            
            // 서버로 메시지 전송
            if (write(socket_fd, buffer, strlen(buffer)) < 0) {
                perror("메시지 전송 실패");
                break;
            }
            
            // 종료 명령 확인
            if (strcmp(buffer, "exit\n") == 0) {
                running = 0;
                break;
            }
        }
        
        // 종료 처리
        kill(child_pid, SIGTERM);  // 자식 프로세스 종료
        close(socket_fd);
    }
    
    return 0;
}

