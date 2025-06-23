//chat_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define MAX_ROOMS 10
#define MAX_ROOM_NAME 50
#define MAX_NICKNAME 50

// 클라이언트 정보 구조체
typedef struct {
    int pid;
    int socket;
    char nickname[MAX_NICKNAME];
    char room[MAX_ROOM_NAME];
    int pipe_parent_to_child[2]; // 부모 -> 자식 통신용 파이프
    int pipe_child_to_parent[2]; // 자식 -> 부모 통신용 파이프
} ClientInfo;

// 채팅방 구조체
typedef struct {
    char name[MAX_ROOM_NAME];
    int active;
} ChatRoom;

// 전역 변수
ClientInfo clients[MAX_CLIENTS];
ChatRoom rooms[MAX_ROOMS];
int client_count = 0;
int room_count = 0;

// 시그널 핸들러 함수
void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    
    // 좀비 프로세스 방지를 위한 waitpid 호출
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // 종료된 클라이언트 정보 삭제
        for (int i = 0; i < client_count; i++) {
            if (clients[i].pid == pid) {
                // 파이프 닫기
                close(clients[i].pipe_parent_to_child[0]);
                close(clients[i].pipe_parent_to_child[1]);
                close(clients[i].pipe_child_to_parent[0]);
                close(clients[i].pipe_child_to_parent[1]);
                
                // 클라이언트 목록에서 제거
                for (int j = i; j < client_count - 1; j++) {
                    clients[j] = clients[j + 1];
                }
                client_count--;
                break;
            }
        }
    }
}

// 메시지 브로드캐스팅 함수
void broadcast_message(char *message, char *room, char *sender) {
    char full_message[BUFFER_SIZE];
    sprintf(full_message, "[%s] %s", sender, message);
    
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].room, room) == 0) {
            // 부모 -> 자식 파이프로 메시지 전송
            write(clients[i].pipe_parent_to_child[1], full_message, strlen(full_message) + 1);
            // SIGUSR1 시그널을 보내 메시지 수신 알림
            kill(clients[i].pid, SIGUSR1);
        }
    }
}

// 귓속말 함수
void whisper_message(char *message, char *target, char *sender) {
    char full_message[BUFFER_SIZE];
    sprintf(full_message, "[귓속말 from %s] %s", sender, message);
    
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].nickname, target) == 0) {
            // 부모 -> 자식 파이프로 메시지 전송
            write(clients[i].pipe_parent_to_child[1], full_message, strlen(full_message) + 1);
            // SIGUSR1 시그널을 보내 메시지 수신 알림
            kill(clients[i].pid, SIGUSR1);
            return;
        }
    }
    
    // 대상을 찾지 못한 경우 발신자에게 알림
    sprintf(full_message, "[시스템] 사용자 %s를 찾을 수 없습니다.", target);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].nickname, sender) == 0) {
            write(clients[i].pipe_parent_to_child[1], full_message, strlen(full_message) + 1);
            kill(clients[i].pid, SIGUSR1);
            break;
        }
    }
}

// 명령어 처리 함수
void process_command(char *command, int client_index) {
    char cmd[BUFFER_SIZE];
    char arg[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    // 명령어와 인자 분리
    sscanf(command, "%s %[^\n]", cmd, arg);
    
    if (strcmp(cmd, "/add") == 0) {
        // 채팅방 추가
        if (room_count < MAX_ROOMS) {
            int exists = 0;
            for (int i = 0; i < room_count; i++) {
                if (strcmp(rooms[i].name, arg) == 0 && rooms[i].active) {
                    exists = 1;
                    break;
                }
            }
            
            if (!exists) {
                strcpy(rooms[room_count].name, arg);
                rooms[room_count].active = 1;
                room_count++;
                sprintf(response, "[시스템] 채팅방 '%s'가 생성되었습니다.", arg);
            } else {
                sprintf(response, "[시스템] 채팅방 '%s'는 이미 존재합니다.", arg);
            }
        } else if (strcmp(cmd, "/rm") == 0) {
        // 채팅방 삭제
        int found = 0;
        for (int i = 0; i < room_count; i++) {
            if (strcmp(rooms[i].name, arg) == 0 && rooms[i].active) {
                rooms[i].active = 0;
                found = 1;
                
                // 해당 방에 있는 모든 사용자를 기본 방으로 이동
                for (int j = 0; j < client_count; j++) {
                    if (strcmp(clients[j].room, arg) == 0) {
                        strcpy(clients[j].room, "lobby");
                        
                        char notice[BUFFER_SIZE];
                        sprintf(notice, "[시스템] 채팅방 '%s'가 삭제되어 로비로 이동되었습니다.", arg);
                        write(clients[j].pipe_parent_to_child[1], notice, strlen(notice) + 1);
                        kill(clients[j].pid, SIGUSR1);
                    }
                }
                
                sprintf(response, "[시스템] 채팅방 '%s'가 삭제되었습니다.", arg);
                break;
            }
        }
        
        if (!found) {
            sprintf(response, "[시스템] 채팅방 '%s'를 찾을 수 없습니다.", arg);
        }
    } else if (strcmp(cmd, "/join") == 0) {
        // 채팅방 입장
        int found = 0;
        for (int i = 0; i < room_count; i++) {
            if (strcmp(rooms[i].name, arg) == 0 && rooms[i].active) {
                found = 1;
                
                // 이전 방에 있는 사용자들에게 퇴장 메시지 전송
                char leave_msg[BUFFER_SIZE];
                sprintf(leave_msg, "[시스템] %s님이 퇴장하셨습니다.", clients[client_index].nickname);
                broadcast_message(leave_msg, clients[client_index].room, "시스템");
                
                // 방 변경
                strcpy(clients[client_index].room, arg);
                
                // 새 방에 있는 사용자들에게 입장 메시지 전송
                char join_msg[BUFFER_SIZE];
                sprintf(join_msg, "[시스템] %s님이 입장하셨습니다.", clients[client_index].nickname);
                broadcast_message(join_msg, clients[client_index].room, "시스템");
                
                sprintf(response, "[시스템] 채팅방 '%s'에 입장하였습니다.", arg);
                break;
            }
        }
        
        if (!found) {
            sprintf(response, "[시스템] 채팅방 '%s'를 찾을 수 없습니다.", arg);
        }
    } else if (strcmp(cmd, "/leave") == 0) {
        // 채팅방 퇴장 (로비로 이동)
        if (strcmp(clients[client_index].room, "lobby") != 0) {
            // 이전 방에 있는 사용자들에게 퇴장 메시지 전송
            char leave_msg[BUFFER_SIZE];
            sprintf(leave_msg, "[시스템] %s님이 퇴장하셨습니다.", clients[client_index].nickname);
            broadcast_message(leave_msg, clients[client_index].room, "시스템");
            
            // 로비로 이동
            strcpy(clients[client_index].room, "lobby");
            sprintf(response, "[시스템] 로비로 이동하였습니다.");
        } else {
            sprintf(response, "[시스템] 이미 로비에 있습니다.");
        }
    } else if (strcmp(cmd, "/list") == 0) {
        // 채팅방 목록 출력
        char room_list[BUFFER_SIZE] = "[시스템] 채팅방 목록:\n";
        int count = 0;
        
        for (int i = 0; i < room_count; i++) {
            if (rooms[i].active) {
                char room_info[100];
                sprintf(room_info, "- %s\n", rooms[i].name);
                strcat(room_list, room_info);
                count++;
            }
        }
        
        if (count == 0) {
            strcat(room_list, "- 활성화된 채팅방이 없습니다.\n");
        }
        
        strcpy(response, room_list);
    } else if (strcmp(cmd, "/users") == 0) {
        // 현재 방의 사용자 목록 출력
        char user_list[BUFFER_SIZE] = "[시스템] 현재 방의 사용자 목록:\n";
        int count = 0;
        
        for (int i = 0; i < client_count; i++) {
            if (strcmp(clients[i].room, clients[client_index].room) == 0) {
                char user_info[100];
                sprintf(user_info, "- %s\n", clients[i].nickname);
                strcat(user_list, user_info);
                count++;
            }
        }
        
        if (count == 0) {
            strcat(user_list, "- 현재 방에 사용자가 없습니다.\n");
        }
        
        strcpy(response, user_list);
    } else {
        sprintf(response, "[시스템] 알 수 없는 명령어입니다.");
    }
    
    // 응답 전송
    write(clients[client_index].pipe_parent_to_child[1], response, strlen(response) + 1);
    kill(clients[client_index].pid, SIGUSR1);
}

// 클라이언트 메시지 처리 함수
void handle_client_message(char *message, int client_index) {
    // 귓속말 처리
    if (message[0] == '!') {
        char cmd[10];
        char target[MAX_NICKNAME];
        char msg[BUFFER_SIZE];
        
        // !whisper 명령어 파싱
        sscanf(message, "%s %s %[^\n]", cmd, target, msg);
        
        if (strcmp(cmd, "!whisper") == 0) {
            whisper_message(msg, target, clients[client_index].nickname);
        } else {
            char response[BUFFER_SIZE];
            sprintf(response, "[시스템] 알 수 없는 명령어입니다.");
            write(clients[client_index].pipe_parent_to_child[1], response, strlen(response) + 1);
            kill(clients[client_index].pid, SIGUSR1);
        }
    }
    // 서버 명령어 처리
    else if (message[0] == '/') {
        process_command(message, client_index);
    }
    // 일반 메시지 처리
    else {
        broadcast_message(message, clients[client_index].room, clients[client_index].nickname);
    }
}

// 클라이언트 프로세스 함수
void client_process(int client_socket, int index) {
    char buffer[BUFFER_SIZE];
    
     // 시그널 핸들러 설정 - 부모로부터 메시지 수신 시 처리
    signal(SIGUSR1, handle_parent_message);
    
    // 닉네임 설정
    write(client_socket, "닉네임을 입력하세요: ", 25);
    int bytes = read(client_socket, buffer, BUFFER_SIZE);
    buffer[bytes - 1] = '\0';  // 개행 문자 제거
    
    strcpy(clients[index].nickname, buffer);
    
    // 초기 방은 로비로 설정
    strcpy(clients[index].room, "lobby");
    
    // 환영 메시지 전송
    char welcome[BUFFER_SIZE];
    sprintf(welcome, "[시스템] %s님 환영합니다! 로비에 입장하셨습니다.", buffer);
    write(client_socket, welcome, strlen(welcome));
    
    // 클라이언트 메시지 처리 루프
    while (1) {
        // 클라이언트로부터 메시지 수신
        bytes = read(client_socket, buffer, BUFFER_SIZE);
        if (bytes <= 0) {
            // 연결 종료
            break;
        }
        
        buffer[bytes - 1] = '\0';  // 개행 문자 제거
        
        // 부모 프로세스에게 메시지 전달
        write(clients[index].pipe_child_to_parent[1], buffer, strlen(buffer) + 1);
        kill(getppid(), SIGUSR2);  // 부모 프로세스에게 시그널 전송
    }
    
    // 연결 종료 처리
    close(client_socket);
    exit(0);
}

// 부모로부터 메시지 수신 처리 함수 (시그널 핸들러)
void handle_parent_message(int sig) {
    char buffer[BUFFER_SIZE];
    int client_index = -1;
    
    // 자신의 인덱스 찾기
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid == getpid()) {
            client_index = i;
            break;
        }
    }
    
    if (client_index != -1) {
        // 부모로부터 메시지 읽기
        int bytes = read(clients[client_index].pipe_parent_to_child[0], buffer, BUFFER_SIZE);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            // 클라이언트에게 메시지 전송
            write(clients[client_index].socket, buffer, strlen(buffer));
        }
    }
}

// 자식으로부터 메시지 수신 처리 함수 (시그널 핸들러)
void handle_child_message(int sig) {
    char buffer[BUFFER_SIZE];
    
    // 모든 클라이언트 파이프 확인
    for (int i = 0; i < client_count; i++) {
        // 파이프에서 데이터 확인 (non-blocking)
        int flags = fcntl(clients[i].pipe_child_to_parent[0], F_GETFL, 0);
        fcntl(clients[i].pipe_child_to_parent[0], F_SETFL, flags | O_NONBLOCK);
        
        int bytes = read(clients[i].pipe_child_to_parent[0], buffer, BUFFER_SIZE);
        
        // 파이프 모드 복원
        fcntl(clients[i].pipe_child_to_parent[0], F_SETFL, flags);
        
        if (bytes > 0) {
            buffer[bytes] = '\0';
            // 메시지 처리
            handle_client_message(buffer, i);
        }
    }
}

void handle_shutdown(int sig) {
    printf("\n[시스템] 서버 종료 시그널 수신. 모든 클라이언트 연결을 종료합니다.\n");
    // 모든 자식 프로세스에게 종료 시그널 (예: SIGTERM) 전송
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid > 0) { // 유효한 PID인 경우
            kill(clients[i].pid, SIGTERM); // 자식에게 종료 시그널
        }
        // 열려있는 모든 파이프 끝 닫기
        close(clients[i].pipe_parent_to_child[0]);
        close(clients[i].pipe_parent_to_child[1]);
        close(clients[i].pipe_child_to_parent[0]);
        close(clients[i].pipe_child_to_parent[1]);
    }
    // 모든 자식 프로세스가 종료될 때까지 기다릴 수도 있음 (waitpid)
    // while (wait(NULL) > 0); // 모든 자식이 종료될 때까지 대기

    close(server_socket_global); // 메인 서버 소켓 닫기 (main 함수 밖에서 접근 가능하게 전역 변수 선언 필요)
    printf("[시스템] 서버가 성공적으로 종료되었습니다.\n");
    exit(0); // 서버 프로세스 종료
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 시그널 핸들러 설정
    signal(SIGCHLD, handle_sigchld);  // 자식 프로세스 종료 처리
    signal(SIGUSR2, handle_child_message);  // 자식 프로세스로부터 메시지 수신 처리
    signal(SIGINT, handle_shutdown); signal(SIGTERM, handle_shutdown);

    // 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("소켓 생성 실패");
        exit(1);
    }
    
    // 소켓 옵션 설정 (주소 재사용)
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8888);  // 포트 번호 설정
    
    // 소켓에 주소 바인딩
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("바인딩 실패");
        exit(1);
    }
    
    // 연결 대기 상태로 전환
    if (listen(server_socket, 5) < 0) {
        perror("리슨 실패");
        exit(1);
    }
    
    printf("채팅 서버가 시작되었습니다. 포트: 8888\n");
    
    // 기본 로비 채팅방 생성
    strcpy(rooms[0].name, "lobby");
    rooms[0].active = 1;
    room_count = 1;
    
    // 데몬 프로세스로 전환
    if (daemon(0, 0) < 0) {
        perror("데몬 전환 실패");
        exit(1);
    }
    
    // 클라이언트 연결 수락 루프
    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("연결 수락 실패");
            continue;
        }
        
        // 최대 클라이언트 수 확인
        if (client_count >= MAX_CLIENTS) {
            write(client_socket, "서버가 가득 찼습니다.", 22);
            close(client_socket);
            continue;
        }
        
        // 파이프 생성
        if (pipe(clients[client_count].pipe_parent_to_child) < 0 || 
            pipe(clients[client_count].pipe_child_to_parent) < 0) {
            perror("파이프 생성 실패");
            close(client_socket);
            continue;
        }
        
        // 자식 프로세스 생성
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("프로세스 생성 실패");
            close(client_socket);
            continue;
        } else if (pid == 0) {
            // 자식 프로세스
            close(server_socket);  // 서버 소켓은 필요 없음
            
            // 클라이언트 처리
            client_process(client_socket, client_count);
            exit(0);
        } else {
            // 부모 프로세스
            close(client_socket);  // 부모는 클라이언트 소켓 필요 없음
            
            // 클라이언트 정보 저장
            clients[client_count].pid = pid;
            clients[client_count].socket = client_socket;
            client_count++;
            
            printf("새로운 클라이언트 연결: %s:%d (총 %d명)\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_count);
        }
    }
    
    // 서버 종료 시 정리
    close(server_socket);
    return 0;
}

