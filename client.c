#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.0.1"  // 로컬호스트 IP, 필요에 따라 변경
#define PORT 8080

int sock; // 전역 변수로 소켓 선언 (시그널 핸들러에서 사용)

// Ctrl+C 시그널 처리 함수
void handle_sigint(int sig) {
    printf("\n채팅을 종료합니다...\n");
    close(sock);
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    char message[BUFFER_SIZE];
    char name[50];
    pid_t pid;
    
    // 소켓 생성
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("소켓 생성 실패");
        return 1;
    }
    
    // 서버 주소 설정
    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    
    // 서버에 연결
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("연결 실패");
        return 1;
    }
    
    printf("서버에 연결되었습니다.\n");
    
    // 사용자 이름 입력
    printf("채팅에 사용할 이름을 입력하세요: ");
    fgets(name, 50, stdin);
    name[strcspn(name, "\n")] = '\0';  // 개행 문자 제거
    
    // 서버에 이름 전송
    if (send(sock, name, strlen(name), 0) < 0) {
        perror("이름 전송 실패");
        return 1;
    }
    
    // Ctrl+C 시그널 핸들러 등록
    signal(SIGINT, handle_sigint);
    
    // fork()를 사용하여 메시지 수신과 전송을 분리
    pid = fork();
    
    if (pid < 0) {
        perror("fork 실패");
        close(sock);
        return 1;
    }
    
    if (pid == 0) {  // 자식 프로세스: 메시지 수신 담당
        char buffer[BUFFER_SIZE];
        int read_size;
        
        while ((read_size = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
            buffer[read_size] = '\0';
            printf("%s", buffer);
        }
        
        if (read_size == 0) {
            printf("서버 연결이 종료되었습니다.\n");
        } else if (read_size == -1) {
            perror("메시지 수신 실패");
        }
        
        // 부모 프로세스에게 종료 신호 전송
        kill(getppid(), SIGINT);
        exit(0);
    } else {  // 부모 프로세스: 메시지 전송 담당
        while (1) {
            fgets(message, BUFFER_SIZE, stdin);
            
            // 'exit' 입력 시 종료
            if (strcmp(message, "exit\n") == 0) {
                send(sock, "exit", 4, 0);
                break;
            }
            
            // 서버에 메시지 전송
            if (send(sock, message, strlen(message), 0) < 0) {
                perror("메시지 전송 실패");
                break;
            }
        }
        
        // 자식 프로세스 종료
        kill(pid, SIGINT);
    }
    
    // 소켓 닫기
    close(sock);
    
    return 0;
}

