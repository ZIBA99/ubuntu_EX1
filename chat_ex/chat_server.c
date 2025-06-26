#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h> // For errno

#define MAX_CLIENTS 10
#define MAX_ROOMS 5
#define MAX_MESSAGE_BUFFER_SIZE (BUFSIZ + 256) // BUFSIZ는 stdio.h에 정의, 일반적으로 8192바이트

// 클라이언트 정보 구조체
typedef struct {
    int sockfd;
    int pid; // 클라이언트 핸들링 자식 프로세스의 PID
    char nickname[32];
    int room_id; // 현재 참여하고 있는 방의 ID (-1이면 방 없음)
    int in_use;  // 현재 사용 중인지 여부 (0: 사용 안함, 1: 사용 중)
    int pipe_read_fd;  // 부모가 자식에게 쓰는 파이프의 쓰기 end (서버 부모 -> 서버 자식)
    int pipe_write_fd; // 자식이 부모에게 쓰는 파이프의 읽기 end (서버 자식 -> 서버 부모)
} client_info;

// 채팅방 정보 구조체
typedef struct {
    char name[32];
    int client_pids[MAX_CLIENTS]; // 방에 참여한 클라이언트의 client_info 인덱스 배열
    int client_count;
    int in_use;
} room_info;

client_info g_clients[MAX_CLIENTS];
room_info g_rooms[MAX_ROOMS];
static int g_client_count = 0; // 전체 연결된 클라이언트 수

// 자식 프로세스에서 자신의 클라이언트 인덱스를 저장하기 위한 static 변수
// 이 변수는 각 자식 프로세스마다 고유한 값을 가집니다.
static int current_child_client_idx = -1;

// 함수 선언
void client_init(int client_idx, int sockfd);
void client_deinit(int client_idx);
int find_client_by_pid(pid_t pid);
int find_client_by_nickname(const char* nickname);
int find_room_by_name(const char* room_name);
int create_room(const char* room_name);
void delete_room(int room_id);
void add_client_to_room(int client_idx, int room_id);
void remove_client_from_room(int client_idx, int room_id);
void send_message_to_room(int room_id, const char* formatted_msg, int sender_sockfd);
void handle_client_message(int client_idx, char* message);
void sigchld_handler(int signo);
void sigHandler_server_child(int signo); // 서버 자식 프로세스를 위한 시그널 핸들러

// 클라이언트 정보 초기화
void client_init(int client_idx, int sockfd) {
    g_clients[client_idx].sockfd = sockfd;
    g_clients[client_idx].pid = 0; // 자식 프로세스 생성 후 업데이트
    snprintf(g_clients[client_idx].nickname, sizeof(g_clients[client_idx].nickname), "guest%d", client_idx);
    g_clients[client_idx].room_id = -1; // 초기에는 어떤 방에도 참여하지 않음
    g_clients[client_idx].in_use = 1;
    g_clients[client_idx].pipe_read_fd = -1;
    g_clients[client_idx].pipe_write_fd = -1;
    g_client_count++;
}

// 클라이언트 정보 비활성화
void client_deinit(int client_idx) {
    if (g_clients[client_idx].in_use) {
        // 방에 참여하고 있었다면 방에서 제거
        if (g_clients[client_idx].room_id != -1) {
            remove_client_from_room(client_idx, g_clients[client_idx].room_id);
        }
        close(g_clients[client_idx].sockfd);
        if (g_clients[client_idx].pipe_read_fd != -1) close(g_clients[client_idx].pipe_read_fd);
        if (g_clients[client_idx].pipe_write_fd != -1) close(g_clients[client_idx].pipe_write_fd);
        memset(&g_clients[client_idx], 0, sizeof(client_info));
        g_clients[client_idx].room_id = -1; // 명시적으로 -1 설정
        g_client_count--;
        printf("Client %d disconnected.\n", client_idx);
    }
}

// PID로 클라이언트 인덱스 찾기
int find_client_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].in_use && g_clients[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

// 닉네임으로 클라이언트 인덱스 찾기
int find_client_by_nickname(const char* nickname) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].in_use && strcmp(g_clients[i].nickname, nickname) == 0) {
            return i;
        }
    }
    return -1;
}

// 방 이름으로 방 인덱스 찾기
int find_room_by_name(const char* room_name) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (g_rooms[i].in_use && strcmp(g_rooms[i].name, room_name) == 0) {
            return i;
        }
    }
    return -1;
}

// 방 생성
int create_room(const char* room_name) {
    for (int i = 0; i < MAX_ROOMS; i++) {
        if (!g_rooms[i].in_use) {
            strncpy(g_rooms[i].name, room_name, sizeof(g_rooms[i].name) - 1);
            g_rooms[i].name[sizeof(g_rooms[i].name) - 1] = '\0';
            g_rooms[i].client_count = 0;
            g_rooms[i].in_use = 1;
            printf("Room '%s' created.\n", room_name);
            return i;
        }
    }
    return -1; // 방 생성 실패 (최대 방 개수 초과)
}

// 방 삭제
void delete_room(int room_id) {
    if (room_id == -1 || !g_rooms[room_id].in_use) return;

    // 방에 있는 모든 클라이언트를 방에서 제거
    for (int i = g_rooms[room_id].client_count - 1; i >= 0; i--) {
        int client_idx = g_rooms[room_id].client_pids[i];
        if (g_clients[client_idx].in_use) {
            g_clients[client_idx].room_id = -1;
            dprintf(g_clients[client_idx].sockfd, "Room '%s' has been deleted. You are now in no room.\n", g_rooms[room_id].name);
            // 자식 프로세스에게 메시지 도착 알림
            kill(g_clients[client_idx].pid, SIGUSR1);
        }
    }
    printf("Room '%s' deleted.\n", g_rooms[room_id].name);
    memset(&g_rooms[room_id], 0, sizeof(room_info));
    g_rooms[room_id].in_use = 0;
}

// 클라이언트를 방에 추가
void add_client_to_room(int client_idx, int room_id) {
    if (client_idx == -1 || room_id == -1 || !g_clients[client_idx].in_use || !g_rooms[room_id].in_use) return;

    if (g_clients[client_idx].room_id != -1) {
        // 이미 다른 방에 있다면 기존 방에서 나감
        remove_client_from_room(client_idx, g_clients[client_idx].room_id);
    }

    if (g_rooms[room_id].client_count < MAX_CLIENTS) {
        g_rooms[room_id].client_pids[g_rooms[room_id].client_count++] = client_idx;
        g_clients[client_idx].room_id = room_id;
        
        char notification_msg[MAX_MESSAGE_BUFFER_SIZE];
        snprintf(notification_msg, sizeof(notification_msg), "%s has joined room '%s'.\n", g_clients[client_idx].nickname, g_rooms[room_id].name);
        send_message_to_room(room_id, notification_msg, -1); // -1: sender_sockfd가 없음을 의미 (서버 공지)
        dprintf(g_clients[client_idx].sockfd, "You have joined room '%s'.\n", g_rooms[room_id].name);
        kill(g_clients[client_idx].pid, SIGUSR1); // Joined message to self
        printf("Client %s joined room '%s'.\n", g_clients[client_idx].nickname, g_rooms[room_id].name);
    } else {
        dprintf(g_clients[client_idx].sockfd, "Room '%s' is full.\n", g_rooms[room_id].name);
        kill(g_clients[client_idx].pid, SIGUSR1);
    }
}

// 클라이언트를 방에서 제거
void remove_client_from_room(int client_idx, int room_id) {
    if (client_idx == -1 || room_id == -1 || !g_clients[client_idx].in_use || !g_rooms[room_id].in_use) return;

    for (int i = 0; i < g_rooms[room_id].client_count; i++) {
        if (g_rooms[room_id].client_pids[i] == client_idx) {
            // 방에서 클라이언트 제거
            for (int j = i; j < g_rooms[room_id].client_count - 1; j++) {
                g_rooms[room_id].client_pids[j] = g_rooms[room_id].client_pids[j+1];
            }
            g_rooms[room_id].client_count--;
            g_clients[client_idx].room_id = -1; // 방 없음으로 설정

            char notification_msg[MAX_MESSAGE_BUFFER_SIZE];
            snprintf(notification_msg, sizeof(notification_msg), "%s has left room '%s'.\n", g_clients[client_idx].nickname, g_rooms[room_id].name);
            send_message_to_room(room_id, notification_msg, -1); // -1: sender_sockfd가 없음을 의미 (서버 공지)
            dprintf(g_clients[client_idx].sockfd, "You have left room '%s'.\n", g_clients[client_idx].room_id == -1 ? "(none)" : g_rooms[g_clients[client_idx].room_id].name); // Use current room_id or "(none)"
            kill(g_clients[client_idx].pid, SIGUSR1); // Left message to self
            printf("Client %s left room '%s'.\n", g_clients[client_idx].nickname, g_rooms[room_id].name);
            
            // 방에 클라이언트가 없으면 방 자동 삭제
            if (g_rooms[room_id].client_count == 0) {
                delete_room(room_id);
            }
            return;
        }
    }
}

// 특정 방의 모든 클라이언트에게 메시지 전송 (보낸 사람 제외)
void send_message_to_room(int room_id, const char* formatted_msg, int sender_sockfd) {
    if (room_id == -1 || !g_rooms[room_id].in_use) return;

    for (int i = 0; i < g_rooms[room_id].client_count; i++) {
        int client_idx = g_rooms[room_id].client_pids[i];
        // 보낸 사람 제외하고 메시지 전송 (sender_sockfd가 -1이면 모두에게 전송)
        if (g_clients[client_idx].in_use && g_clients[client_idx].sockfd != sender_sockfd) {
            // 부모가 자식에게 메시지를 파이프로 전달
            write(g_clients[client_idx].pipe_read_fd, formatted_msg, strlen(formatted_msg) + 1);
            // 자식 프로세스에게 메시지 도착 알림
            kill(g_clients[client_idx].pid, SIGUSR1);
        }
    }
}

// 귓속말 전송
void send_whisper(int sender_client_idx, const char* target_nickname, const char* message) {
    int target_client_idx = find_client_by_nickname(target_nickname);
    if (target_client_idx == -1 || !g_clients[target_client_idx].in_use) {
        dprintf(g_clients[sender_client_idx].sockfd, "Error: User '%s' not found or offline.\n", target_nickname);
        kill(g_clients[sender_client_idx].pid, SIGUSR1);
        return;
    }

    char whisper_msg[MAX_MESSAGE_BUFFER_SIZE];
    snprintf(whisper_msg, sizeof(whisper_msg), "[Whisper from %s]: %s\n", g_clients[sender_client_idx].nickname, message);

    // 대상 클라이언트에게 직접 메시지 전송 (파이프를 통해 해당 자식 프로세스로)
    write(g_clients[target_client_idx].pipe_read_fd, whisper_msg, strlen(whisper_msg) + 1);
    kill(g_clients[target_client_idx].pid, SIGUSR1);
    
    // 보낸 사람에게도 전송 확인 메시지
    dprintf(g_clients[sender_client_idx].sockfd, "[Whisper to %s]: %s\n", target_nickname, message);
    kill(g_clients[sender_client_idx].pid, SIGUSR1);

    printf("Whisper from %s to %s: %s\n", g_clients[sender_client_idx].nickname, target_nickname, message);
}


// 클라이언트로부터 받은 메시지 처리
void handle_client_message(int client_idx, char* message) {
    if (client_idx == -1 || !g_clients[client_idx].in_use) return;

    // 메시지 끝의 개행 문자 제거
    message[strcspn(message, "\n")] = 0;

    char response_msg[MAX_MESSAGE_BUFFER_SIZE];

    // 명령어 처리
    if (message[0] == '/') {
        char command[32];
        char arg[MAX_MESSAGE_BUFFER_SIZE];
        int scan_count = sscanf(message, "/%s %s", command, arg); // sscanf 반환 값 확인

        if (scan_count >= 1) { // 명령어가 하나라도 파싱되었는지 확인
            if (strcmp(command, "add") == 0) {
                if (scan_count < 2) {
                    snprintf(response_msg, sizeof(response_msg), "Usage: /add <room_name>\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else if (find_room_by_name(arg) != -1) {
                    snprintf(response_msg, sizeof(response_msg), "Error: Room '%s' already exists.\n", arg);
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else if (create_room(arg) != -1) {
                    snprintf(response_msg, sizeof(response_msg), "Room '%s' created.\n", arg);
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else {
                    snprintf(response_msg, sizeof(response_msg), "Error: Could not create room '%s'.\n", arg);
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                }
            } else if (strcmp(command, "rm") == 0) {
                if (scan_count < 2) {
                    snprintf(response_msg, sizeof(response_msg), "Usage: /rm <room_name>\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else {
                    int room_id = find_room_by_name(arg);
                    if (room_id != -1) {
                        delete_room(room_id);
                        snprintf(response_msg, sizeof(response_msg), "Room '%s' deleted.\n", arg);
                        dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                    } else {
                        snprintf(response_msg, sizeof(response_msg), "Error: Room '%s' not found.\n", arg);
                        dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                    }
                }
            } else if (strcmp(command, "join") == 0) {
                if (scan_count < 2) {
                    snprintf(response_msg, sizeof(response_msg), "Usage: /join <room_name>\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else {
                    int room_id = find_room_by_name(arg);
                    if (room_id == -1) {
                        snprintf(response_msg, sizeof(response_msg), "Error: Room '%s' not found.\n", arg);
                        dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                    } else {
                        add_client_to_room(client_idx, room_id);
                    }
                }
            } else if (strcmp(command, "nick") == 0) {
                if (scan_count < 2) {
                    snprintf(response_msg, sizeof(response_msg), "Usage: /nick <new_nickname>\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else if (strlen(arg) < 2 || strlen(arg) > 31) {
                    snprintf(response_msg, sizeof(response_msg), "Error: Nickname must be between 2 and 31 characters.\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else if (find_client_by_nickname(arg) != -1) {
                    snprintf(response_msg, sizeof(response_msg), "Error: Nickname '%s' is already in use.\n", arg);
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else {
                    char old_nickname[32];
                    strncpy(old_nickname, g_clients[client_idx].nickname, sizeof(old_nickname) -1);
                    old_nickname[sizeof(old_nickname) -1] = '\0';
                    
                    strncpy(g_clients[client_idx].nickname, arg, sizeof(g_clients[client_idx].nickname) - 1);
                    g_clients[client_idx].nickname[sizeof(g_clients[client_idx].nickname) - 1] = '\0';
                    snprintf(response_msg, sizeof(response_msg), "Your nickname has been changed to '%s'.\n", g_clients[client_idx].nickname);
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                    
                    if (g_clients[client_idx].room_id != -1) {
                        char notification_msg[MAX_MESSAGE_BUFFER_SIZE];
                        snprintf(notification_msg, sizeof(notification_msg), "%s has changed nickname to %s.\n", old_nickname, g_clients[client_idx].nickname);
                        send_message_to_room(g_clients[client_idx].room_id, notification_msg, -1);
                    }
                }
            } else if (strcmp(command, "leave") == 0) {
                if (g_clients[client_idx].room_id != -1) {
                    remove_client_from_room(client_idx, g_clients[client_idx].room_id);
                } else {
                    snprintf(response_msg, sizeof(response_msg), "You are not in any room.\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                }
            } else if (strcmp(command, "list") == 0) {
                snprintf(response_msg, sizeof(response_msg), "Available Rooms:\n");
                for (int i = 0; i < MAX_ROOMS; i++) {
                    if (g_rooms[i].in_use) {
                        char room_info_str[64];
                        snprintf(room_info_str, sizeof(room_info_str), "- %s (%d/%d)\n", g_rooms[i].name, g_rooms[i].client_count, MAX_CLIENTS);
                        strncat(response_msg, room_info_str, sizeof(response_msg) - strlen(response_msg) - 1);
                    }
                }
                dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
            } else if (strcmp(command, "users") == 0) {
                if (g_clients[client_idx].room_id != -1) {
                    int room_id = g_clients[client_idx].room_id;
                    snprintf(response_msg, sizeof(response_msg), "Users in room '%s':\n", g_rooms[room_id].name);
                    for (int i = 0; i < g_rooms[room_id].client_count; i++) {
                        char user_info_str[64];
                        snprintf(user_info_str, sizeof(user_info_str), "- %s\n", g_clients[g_rooms[room_id].client_pids[i]].nickname);
                        strncat(response_msg, user_info_str, sizeof(response_msg) - strlen(response_msg) - 1);
                    }
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                } else {
                    snprintf(response_msg, sizeof(response_msg), "You are not in any room.\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                }
            } else {
                snprintf(response_msg, sizeof(response_msg), "Unknown command: /%s\n", command);
                dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
            }
        } else {
            snprintf(response_msg, sizeof(response_msg), "Invalid command format: %s\n", message);
            dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
        }
        kill(g_clients[client_idx].pid, SIGUSR1); // Command response sent
    } else if (message[0] == '!' && strncmp(message, "!whisper ", 9) == 0) {
        // 귓속말 처리
        char target_nickname[32];
        char* whisper_msg_ptr;

        // !whisper [닉네임] [메시지] 파싱
        whisper_msg_ptr = strstr(message, " "); // 첫 번째 공백 찾기
        if (whisper_msg_ptr != NULL) {
            whisper_msg_ptr++; // 공백 다음으로 이동
            int parsed = sscanf(whisper_msg_ptr, "%31s", target_nickname); // 닉네임 추출 (버퍼 오버플로우 방지)

            if (parsed == 1) { // 닉네임이 성공적으로 파싱되었는지 확인
                whisper_msg_ptr = strstr(whisper_msg_ptr, " "); // 닉네임 다음 공백 찾기
                if (whisper_msg_ptr != NULL) {
                    whisper_msg_ptr++; // 메시지 시작 지점
                    send_whisper(client_idx, target_nickname, whisper_msg_ptr);
                } else {
                    snprintf(response_msg, sizeof(response_msg), "Error: Message missing. Usage: !whisper <nickname> <message>\n");
                    dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                    kill(g_clients[client_idx].pid, SIGUSR1);
                }
            } else {
                snprintf(response_msg, sizeof(response_msg), "Error: Nickname missing. Usage: !whisper <nickname> <message>\n");
                dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
                kill(g_clients[client_idx].pid, SIGUSR1);
            }
        } else {
            snprintf(response_msg, sizeof(response_msg), "Error: Invalid whisper format. Usage: !whisper <nickname> <message>\n");
            dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
            kill(g_clients[client_idx].pid, SIGUSR1);
        }
    }
    // 일반 메시지
    else {
        if (g_clients[client_idx].room_id == -1) {
            snprintf(response_msg, sizeof(response_msg), "You are not in any room. Use /join [room_name] to join a room.\n");
            dprintf(g_clients[client_idx].sockfd, "%s", response_msg);
            kill(g_clients[client_idx].pid, SIGUSR1);
        } else {
            char chat_msg[MAX_MESSAGE_BUFFER_SIZE];
            snprintf(chat_msg, sizeof(chat_msg), "%s: %s\n", g_clients[client_idx].nickname, message);
            send_message_to_room(g_clients[client_idx].room_id, chat_msg, g_clients[client_idx].sockfd);
        }
    }
}

// SIGCHLD 핸들러: 자식 프로세스가 종료될 때 호출되어 좀비 프로세스를 방지
void sigchld_handler(int signo) {
    pid_t pid;
    int status;
    // 모든 자식 프로세스가 종료될 때까지 기다림
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int client_idx = find_client_by_pid(pid);
        if (client_idx != -1) {
            printf("Client handler (PID %d) for client %s exited.\n", pid, g_clients[client_idx].nickname);
            client_deinit(client_idx);
        }
    }
}

// 서버의 자식 프로세스에서 SIGUSR1을 처리하는 핸들러
void sigHandler_server_child(int signo) {
    if (signo == SIGUSR1) {
        if (current_child_client_idx != -1 && g_clients[current_child_client_idx].in_use) {
            char buf_from_parent_pipe[MAX_MESSAGE_BUFFER_SIZE];
            memset(buf_from_parent_pipe, 0, MAX_MESSAGE_BUFFER_SIZE);

            // 부모로부터 파이프를 통해 메시지 읽기
            // 이 read는 SIGUSR1 시그널이 왔으므로 데이터가 있을 것으로 가정합니다.
            ssize_t pipe_n = read(g_clients[current_child_client_idx].pipe_read_fd, buf_from_parent_pipe, MAX_MESSAGE_BUFFER_SIZE - 1);

            if (pipe_n > 0) {
                buf_from_parent_pipe[pipe_n] = '\0';
                // 읽은 메시지를 실제 클라이언트 애플리케이션의 소켓으로 전송
                if (write(g_clients[current_child_client_idx].sockfd, buf_from_parent_pipe, strlen(buf_from_parent_pipe)) < 0) {
                    // 오류 처리 (데몬이므로 perror는 시스템 로그에 기록될 수 있음)
                    // fprintf(stderr, "Error writing to client socket in server_child sigHandler: %s\n", strerror(errno));
                }
            } else if (pipe_n == 0) {
                // 부모 파이프가 닫혔음 (예: 서버 종료)
                g_clients[current_child_client_idx].in_use = 0; // 클라이언트 비활성화 표시
            } else {
                // 읽기 오류 (예: EINTR, 하지만 SA_RESTART로 자동 재시작될 가능성 높음)
                // fprintf(stderr, "Error reading from parent pipe in server_child sigHandler: %s\n", strerror(errno));
            }
        }
    }
}


int main(int argc, char** argv) {
    int listen_sockfd, client_sockfd;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_addr_size;
    int port_no;
    pid_t pid;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return -1;
    }
    port_no = atoi(argv[1]);

    // 데몬 프로세스 생성
    pid_t daemon_pid = fork();
    if (daemon_pid < 0) {
        perror("fork (daemon)");
        exit(EXIT_FAILURE);
    }
    if (daemon_pid > 0) {
        // 부모 프로세스 종료
        exit(EXIT_SUCCESS);
    }
    // 자식 프로세스가 새 세션의 리더가 됨
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }
    // 표준 입출력 닫기 (데몬이므로)
    close(STDIN_FILENO);
    // stdout과 stderr은 printf/perror 디버깅을 위해 잠시 열어두거나 로그 파일로 리다이렉트 할 수 있음
    // 여기서는 간단히 닫습니다. 실제 데몬에서는 로그 파일로 리다이렉트가 필요합니다.
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // SIGCHLD 시그널 핸들러 등록 (부모 프로세스용)
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; // SA_RESTART: 시스템 호출 재시작, SA_NOCLDSTOP: 자식 정지 무시
    if (sigaction(SIGCHLD, &sa_chld, 0) == -1) {
        perror("sigaction for SIGCHLD");
        exit(EXIT_FAILURE);
    }

    // 클라이언트 및 방 정보 초기화
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&g_clients[i], 0, sizeof(client_info));
        g_clients[i].room_id = -1; // -1로 초기화
    }
    for (int i = 0; i < MAX_ROOMS; i++) {
        memset(&g_rooms[i], 0, sizeof(room_info));
    }

    listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_no);

    if (bind(listen_sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_sockfd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sockfd, 5) < 0) {
        perror("listen");
        close(listen_sockfd);
        exit(EXIT_FAILURE);
    }

    // 데몬이므로 직접 stdout에 출력되지 않음. 로그 파일에 기록해야 함.
    // printf("Server started on port %d\n", port_no); 

    while (1) {
        client_addr_size = sizeof(client_addr);
        client_sockfd = accept(listen_sockfd, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_sockfd < 0) {
            if (errno == EINTR) {
                continue; // 시그널에 의해 accept가 중단된 경우 다시 시도
            }
            perror("accept");
            continue;
        }

        int client_idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!g_clients[i].in_use) {
                client_idx = i;
                break;
            }
        }

        if (client_idx == -1) {
            // 데몬이므로 직접 dprintf 대신 로그 함수 사용 고려
            dprintf(client_sockfd, "Server is full. Please try again later.\n");
            close(client_sockfd);
            continue;
        }

        // 클라이언트 핸들링을 위한 파이프 생성 (부모-자식 통신용)
        int pfd_parent_read[2]; // 부모가 자식에게 쓸 파이프 (부모: pfd_parent_read[1], 자식: pfd_parent_read[0])
        int pfd_child_write[2]; // 자식이 부모에게 쓸 파이프 (자식: pfd_child_write[1], 부모: pfd_child_write[0])

        if (pipe(pfd_parent_read) == -1 || pipe(pfd_child_write) == -1) {
            perror("pipe");
            close(client_sockfd);
            continue;
        }

        pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_sockfd);
            close(pfd_parent_read[0]); close(pfd_parent_read[1]);
            close(pfd_child_write[0]); close(pfd_child_write[1]);
            continue;
        } else if (pid == 0) { // 자식 프로세스 (클라이언트 핸들러)
            close(listen_sockfd); // 자식은 리스닝 소켓을 닫음
            client_init(client_idx, client_sockfd); // 자식 프로세스에서 클라이언트 정보 초기화 (PID는 아직 0)
            g_clients[client_idx].pid = getpid(); // 실제 자식 PID로 업데이트 (중요!)

            // 파이프 설정
            close(pfd_parent_read[1]);  // 자식은 부모에게 쓰는 파이프의 쓰기 끝 닫음 (읽기만 함)
            close(pfd_child_write[0]);  // 자식은 부모에게 읽는 파이프의 읽기 끝 닫음 (쓰기만 함)
            
            g_clients[client_idx].pipe_read_fd = pfd_parent_read[0]; // 자식의 읽기용 파이프 (부모로부터 받음)
            g_clients[client_idx].pipe_write_fd = pfd_child_write[1]; // 자식의 쓰기용 파이프 (부모에게 보냄)

            // 이 자식 프로세스에서 사용할 client_idx를 static 변수에 저장
            current_child_client_idx = client_idx;

            // 자식 프로세스에서 SIGUSR1 핸들러 등록
            struct sigaction sa_usr1;
            memset(&sa_usr1, 0, sizeof(sa_usr1));
            sa_usr1.sa_handler = sigHandler_server_child; // 서버 자식용 시그널 핸들러
            sigemptyset(&sa_usr1.sa_mask);
            sa_usr1.sa_flags = SA_RESTART; // 시스템 호출이 시그널에 의해 중단될 경우 재시작
            if (sigaction(SIGUSR1, &sa_usr1, NULL) == -1) {
                // 오류 처리 (데몬이므로 stderr 대신 로그 파일 고려)
                // perror("sigaction for SIGUSR1 in server child");
                exit(EXIT_FAILURE);
            }

            printf("New client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            printf("Client added: PID %d, Nickname: %s\n", g_clients[client_idx].pid, g_clients[client_idx].nickname);
            dprintf(g_clients[client_idx].sockfd, "Welcome, %s! Use /join <room_name> to chat.\n", g_clients[client_idx].nickname);
            // 클라이언트에게 환영 메시지 보내고 알림 시그널 전송 (클라이언트 앱으로)
            // 주의: 이 kill은 서버 자식 프로세스에서 실행되며, 시그널이 클라이언트 앱으로 직접 전송되려면
            // 클라이언트 앱의 PID를 서버 자식이 알고 있어야 합니다. 현재는 이 시그널이 서버 자식 자신에게 도달합니다.
            // 클라이언트 앱에 메시지가 전달되려면, 서버 자식이 소켓에 데이터를 쓴 후 클라이언트 앱에 SIGUSR1을 보내야 합니다.
            // 하지만 클라이언트 앱의 PID를 서버 자식이 모릅니다.
            // 이 과제는 시그널 전달 방식에 모호함이 있습니다.
            // 현재 클라이언트 앱은 자신의 SIGUSR1 핸들러에서 소켓으로부터 데이터를 읽도록 되어 있습니다.
            // 따라서 서버 자식이 소켓에 write하면 클라이언트 앱이 read할 것입니다.
            // 여기서는 클라이언트 앱이 시그널 없이 소켓에서 읽는 경우에도 작동할 수 있습니다.
            // 하지만 '비동기적 이벤트 처리'가 시그널을 통한 폴링이라면, 서버 자식은
            // 클라이언트 앱의 PID를 알거나, 클라이언트 앱은 소켓을 지속적으로 읽어야 합니다.
            kill(g_clients[client_idx].pid, SIGUSR1); // 서버 자식 자신에게 보내는 시그널이 됨.
                                                    // 실제 클라이언트 앱에 대한 시그널은 클라이언트 앱의 read 호출을
                                                    // SA_RESTART와 함께 사용하여 처리하는 방식으로 작동해야 합니다.

            // 클라이언트 핸들러 루프
            char buf[MAX_MESSAGE_BUFFER_SIZE];
            ssize_t n;

            while (g_clients[client_idx].in_use) {
                memset(buf, 0, MAX_MESSAGE_BUFFER_SIZE);
                // 클라이언트 소켓으로부터 읽기 (블로킹 호출)
                n = read(g_clients[client_idx].sockfd, buf, MAX_MESSAGE_BUFFER_SIZE - 1);

                if (n > 0) {
                    buf[n] = '\0';
                    printf("Received from client %s (PID: %d): %zd:%s", g_clients[client_idx].nickname, getpid(), n, buf);
		    handle_client_message(client_idx, buf); // 메시지 처리 (명령어, 브로드캐스트 등)
                } else if (n == 0) {
                    // 클라이언트 연결 종료
                    printf("Client %s (PID %d) disconnected.\n", g_clients[client_idx].nickname, getpid());
                    break; // 루프 종료
                } else { // n < 0, 오류 또는 시그널에 의해 중단
                    if (errno == EINTR) {
                        // read가 시그널에 의해 중단되었지만, SA_RESTART 플래그로 인해 자동 재시작될 것입니다.
                        // 따라서 여기 도달할 일은 드뭅니다.
                        // 만약 도달한다면, 다른 비-재시작 시그널에 의한 중단일 수 있습니다.
                        continue; // 루프 계속
                    } else {
                        perror("read from client socket in server child");
                        break; // 다른 오류, 루프 종료
                    }
                }
            }

            // 루프 종료 시 클라이언트 핸들러 정리
            close(g_clients[client_idx].sockfd);
            close(g_clients[client_idx].pipe_read_fd);
            close(g_clients[client_idx].pipe_write_fd);
            exit(0); // 자식 프로세스 종료
        } else { // 부모 프로세스 (클라이언트 연결 관리)
            // 부모는 자식이 사용할 소켓을 이미 fork 전에 넘겨줬으므로, 부모는 자식 소켓 닫음
            close(client_sockfd); 
            // 파이프 설정: 부모는 자식에게 쓸 파이프의 쓰기 끝 (pfd_parent_read[1])만 사용
            // 자식으로부터 읽을 파이프의 읽기 끝 (pfd_child_write[0])만 사용
            close(pfd_parent_read[0]); // 부모는 읽기 끝 닫음
            close(pfd_child_write[1]); // 부모는 쓰기 끝 닫음

            // 전역 클라이언트 정보에 파이프 FD 및 자식 PID 저장
            g_clients[client_idx].pid = pid;
            g_clients[client_idx].pipe_read_fd = pfd_parent_read[1]; // 부모가 자식에게 쓸 FD
            g_clients[client_idx].pipe_write_fd = pfd_child_write[0]; // 부모가 자식으로부터 읽을 FD

            // 이 부모 루프는 계속해서 accept()를 수행합니다.
            // 자식으로부터 오는 메시지 (pfd_child_write[0])를 처리하려면 이 루프에 select()나 시그널이 필요하지만
            // 현재 과제 요구사항에 따르면 자식->부모 통신은 직접적으로 메시지 처리와 연결되지 않을 수 있습니다.
        }
    }

    close(listen_sockfd);
    return 0;
}
