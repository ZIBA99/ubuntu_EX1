#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

// 클라이언트 정보를 저장할 구조체
typedef struct {
    int socket;
    char name[50];
    pid_t pid;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;

// 좀비 프로세스 방지를 위한 시그널 핸들러
void sigchld_handler(int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// 모든 클라이언트에게 메시지 전송
void broadcast_message(char *message, int sender_socket) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].socket != sender_socket) {
            write(clients[i].socket, message, strlen(message));
        }
    }
}

// 클라이언트 처리 함수
void handle_client(int client_socket, int client_index) {
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE + 50];
    int bytes_received;
    
    // 클라이언트 이름 받기
    bytes_received = read(client_socket, buffer, BUFFER_SIZE);
    buffer[bytes_received] = '\0';
    
    strcpy(clients[client_index].name, buffer);
    sprintf(message, "%s님이 채팅방에 입장했습니다.\n", clients[client_index].name);
    printf("%s", message);
    broadcast_message(message, client_socket);
    
    // 클라이언트로부터 메시지 수신 및 브로드캐스트
    while ((bytes_received = read(client_socket, buffer, BUFFER_SIZE)) > 0) {
        buffer[bytes_received] = '\0';
        
        // 종료 메시지 확인
        if (strcmp(buffer, "exit\n") == 0) {
            break;
        }
        
        sprintf(message, "%s: %s", clients[client_index].name, buffer);
        printf("%s", message);
        broadcast_message(message, client_socket);
    }
    
    // 클라이언트 연결 종료 처리
    sprintf(message, "%s님이 채팅방을 나갔습니다.\n", clients[client_index].name);
    printf("%s", message);
    broadcast_message(message, client_socket);
    
    close(client_socket);
    
    exit(0);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct sigaction sa;
    
    // 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("소켓 생성 실패");
        exit(1);
    }
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // 소켓 옵션 설정 - 주소 재사용
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt 실패");
        exit(1);
    }
    
    // 소켓에 주소 바인딩
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("바인딩 실패");
        exit(1);
    }
    
    // 연결 대기
    if (listen(server_socket, 5) < 0) {
        perror("리스닝 실패");
        exit(1);
    }
    
    // 시그널 핸들러 설정
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction 실패");
        exit(1);
    }
    
    printf("채팅 서버가 포트 %d에서 실행 중입니다...\n", PORT);
    
    // 클라이언트 연결 수락 및 처리
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("연결 수락 실패");
            continue;
        }
        
        printf("새 클라이언트 연결: %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // 최대 클라이언트 수 확인
        if (client_count >= MAX_CLIENTS) {
            printf("최대 클라이언트 수 초과. 연결 거부.\n");
            close(client_socket);
            continue;
        }
        
        // 클라이언트 정보 저장
        clients[client_count].socket = client_socket;
        
        // fork()를 사용하여 클라이언트 처리
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork 실패");
            exit(1);
        }
        
        if (pid == 0) {  // 자식 프로세스
            close(server_socket);  // 자식은 서버 소켓 필요 없음
            handle_client(client_socket, client_count);
        } else {  // 부모 프로세스
            clients[client_count].pid = pid;
            client_count++;
            close(client_socket);  // 부모는 클라이언트 소켓 필요 없음
        }
    }
    
    close(server_socket);
    return 0;
}

