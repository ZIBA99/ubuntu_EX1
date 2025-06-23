#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h> // fcntl을 사용하여 논블로킹 모드 설정
#include <sys/stat.h> // umask를 위해
#include <time.h> // 시간 기록을 위해

#define PORT 8080
#define MAX_CLIENTS 10          // 최대 동시 접속 클라이언트 수
#define BUFFER_SIZE 1024        // 통신 버퍼 크기 (각 메시지 부분의 최대 크기)
#define MAX_ROOMS 5             // 최대 채팅방 수
#define MAX_NICKNAME_LEN 31     // 닉네임 최대 길이 (NULL 포함)
#define MAX_ROOMNAME_LEN 31     // 채팅방 이름 최대 길이 (NULL 포함)

// 메시지 타입 정의 (프로토콜)
#define MSG_TYPE_CHAT       "CHAT"      // 일반 채팅 메시지
#define MSG_TYPE_COMMAND    "CMD"       // 서버 명령어
#define MSG_TYPE_WHISPER    "WHISPER"   // 귓속말
#define MSG_TYPE_JOIN       "JOIN"      // 채팅방 입장 알림
#define MSG_TYPE_LEAVE      "LEAVE"     // 채팅방 퇴장 알림
#define MSG_TYPE_INFO       "INFO"      // 서버 정보 메시지 (예: 명령어 결과, 오류)

// 클라이언트 정보를 저장할 구조체
typedef struct {
    pid_t pid;           // 자식 프로세스 ID
    int pipe_read_fd;    // 자식 -> 부모 파이프의 읽기 FD (부모용)
    int pipe_write_fd;   // 부모 -> 자식 파이프의 쓰기 FD (부모용)
    char nickname[MAX_NICKNAME_LEN + 1]; // 클라이언트 닉네임
    char room_name[MAX_ROOMNAME_LEN + 1]; // 현재 참여 중인 채팅방 이름
} client_info_t;

// 채팅방 정보를 저장할 구조체
typedef struct {
    char name[MAX_ROOMNAME_LEN + 1];
    int client_count;
} chat_room_t;

client_info_t clients[MAX_CLIENTS]; // 연결된 클라이언트 정보 배열
int client_count = 0;               // 현재 연결된 클라이언트 수

chat_room_t chat_rooms[MAX_ROOMS]; // 채팅방 정보 배열
int room_count = 0;                // 현재 개설된 채팅방 수

// ===========================================
// 함수 선언
// ===========================================
void daemonize();
void sigchld_handler(int signo);
void set_nonblocking(int fd); // 파일 디스크립터를 논블로킹으로 설정하는 함수

// 클라이언트 처리 (자식 프로세스)
void handle_client_child_process(int client_fd, int parent_to_child_read_fd, int child_to_parent_write_fd);

// 부모 프로세스 로직
void parent_main_loop(int server_socket);
void add_client_to_list(pid_t pid, int pipe_read_fd, int pipe_write_fd, const char* initial_nickname, const char* initial_room);
void remove_client_from_list(pid_t pid);
void process_message_from_child(const char *message, int sender_pipe_read_fd);

// 메시지 전송 및 브로드캐스트
void send_message_to_client_by_pid(pid_t target_pid, const char *message);
void send_message_to_client_by_nickname(const char *nickname, const char *message);
void broadcast_message_in_room(const char *room, const char *message, pid_t sender_pid);
void broadcast_message_to_all_clients(const char *message, int sender_pipe_read_fd); // 디버깅용 또는 서버 전체 공지용

// 채팅방 관리
int add_room(const char *room_name);
int remove_room(const char *room_name);
int find_room_index(const char *room_name);
int join_room(pid_t pid, const char *room_name);
int leave_room(pid_t pid);
void get_room_list_message(char *buffer, size_t buf_size);
void get_users_in_room_message(const char *room_name, char *buffer, size_t buf_size);

// 유틸리티
char* get_current_time_str();


// ===========================================
// 데몬화 함수
// ===========================================
void daemonize() {
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        perror("첫 번째 fork 실패");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // 부모 프로세스 종료
    }

    if (setsid() < 0) { // 새 세션 생성
        perror("setsid 실패");
        exit(EXIT_FAILURE);
    }

    pid = fork(); // 두 번째 fork (세션 리더가 터미널 다시 얻는 것 방지)
    if (pid < 0) {
        perror("두 번째 fork 실패");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS); // 첫 번째 자식 프로세스 종료 (데몬은 손자가 됨)
    }

    if (chdir("/") < 0) { // 작업 디렉토리를 루트로 변경
        perror("chdir 실패");
        exit(EXIT_FAILURE);
    }

    umask(0); // 파일 생성 마스크를 0으로 설정

    // 표준 입출력 파일 디스크립터를 /dev/null로 리다이렉션
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDWR); // stdin
    dup(0); // stdout
    dup(0); // stderr
}

// ===========================================
// SIGCHLD 시그널 핸들러
// ===========================================
void sigchld_handler(int signo) {
    pid_t pid;
    int status;
    // Non-blocking waitpid로 종료된 모든 자식 프로세스 처리
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[%s][서버] 자식 프로세스 %d 종료 처리됨.\n", get_current_time_str(), pid);
        remove_client_from_list(pid); // clients 배열에서 해당 클라이언트 정보 제거
    }
}

// ===========================================
// 파일 디스크립터를 논블로킹 모드로 설정
// ===========================================
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL 실패");
        exit(EXIT_FAILURE);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK 실패");
        exit(EXIT_FAILURE);
    }
}

// ===========================================
// 유틸리티 함수: 현재 시간 문자열 반환
// ===========================================
char* get_current_time_str() {
    static char time_str[30];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
    return time_str;
}

// ===========================================
// 클라이언트 정보 추가/제거 (부모 프로세스용)
// ===========================================
void add_client_to_list(pid_t pid, int pipe_read_fd, int pipe_write_fd, const char* initial_nickname, const char* initial_room) {
    if (client_count >= MAX_CLIENTS) {
        fprintf(stderr, "[%s][서버] 클라이언트 목록이 가득 찼습니다.\n", get_current_time_str());
        return;
    }
    clients[client_count].pid = pid;
    clients[client_count].pipe_read_fd = pipe_read_fd;
    clients[client_count].pipe_write_fd = pipe_write_fd;
    strncpy(clients[client_count].nickname, initial_nickname, MAX_NICKNAME_LEN);
    clients[client_count].nickname[MAX_NICKNAME_LEN] = '\0';
    strncpy(clients[client_count].room_name, initial_room, MAX_ROOMNAME_LEN);
    clients[client_count].room_name[MAX_ROOMNAME_LEN] = '\0';
    client_count++;
}

void remove_client_from_list(pid_t pid) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid == pid) {
            printf("[%s][서버] 클라이언트 %s(%d) 퇴장 처리.\n", get_current_time_str(), clients[i].nickname, pid);

            // 해당 클라이언트가 속한 방의 사용자 수 감소
            int room_idx = find_room_index(clients[i].room_name);
            if (room_idx != -1) {
                chat_rooms[room_idx].client_count--;
                // 방에 남은 사용자가 없다면 방 자동 삭제 (선택 사항)
                if (chat_rooms[room_idx].client_count == 0 && strcmp(chat_rooms[room_idx].name, "general") != 0) {
                    printf("[%s][서버] 채팅방 '%s'에 더 이상 사용자가 없어 삭제합니다.\n", get_current_time_str(), chat_rooms[room_idx].name);
                    remove_room(chat_rooms[room_idx].name);
                }
            }

            close(clients[i].pipe_read_fd);
            close(clients[i].pipe_write_fd);

            // 배열에서 제거 (마지막 요소를 현재 위치로 이동)
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j+1];
            }
            client_count--;
            printf("[%s][서버] 클라이언트 정보 제거 완료. 현재 클라이언트 수: %d\n", get_current_time_str(), client_count);
            break;
        }
    }
}

// ===========================================
// 채팅방 관리 함수 (부모 프로세스)
// ===========================================
int find_room_index(const char *room_name) {
    for (int i = 0; i < room_count; i++) {
        if (strcmp(chat_rooms[i].name, room_name) == 0) {
            return i;
        }
    }
    return -1;
}

int add_room(const char *room_name) {
    if (find_room_index(room_name) != -1) {
        return -1; // 이미 존재하는 방
    }
    if (room_count >= MAX_ROOMS) {
        return -2; // 최대 방 개수 초과
    }
    strncpy(chat_rooms[room_count].name, room_name, MAX_ROOMNAME_LEN);
    chat_rooms[room_count].name[MAX_ROOMNAME_LEN] = '\0';
    chat_rooms[room_count].client_count = 0;
    room_count++;
    printf("[%s][서버] 채팅방 '%s' 생성 완료. (총 %d개)\n", get_current_time_str(), room_name, room_count);
    return 0;
}

int remove_room(const char *room_name) {
    if (strcmp(room_name, "general") == 0) { // 기본방은 삭제 불가
        return -3;
    }
    int idx = find_room_index(room_name);
    if (idx == -1) {
        return -1; // 존재하지 않는 방
    }
    if (chat_rooms[idx].client_count > 0) {
        return -2; // 방에 사용자가 남아있음
    }

    // 배열에서 제거 (마지막 요소를 현재 위치로 이동)
    for (int i = idx; i < room_count - 1; i++) {
        chat_rooms[i] = chat_rooms[i+1];
    }
    room_count--;
    printf("[%s][서버] 채팅방 '%s' 삭제 완료. (총 %d개)\n", get_current_time_str(), room_name, room_count);
    return 0;
}

int join_room(pid_t pid, const char *room_name) {
    int client_idx = -1;
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid == pid) {
            client_idx = i;
            break;
        }
    }
    if (client_idx == -1) return -1; // 클라이언트 정보 없음

    int old_room_idx = find_room_index(clients[client_idx].room_name);
    if (old_room_idx != -1) {
        chat_rooms[old_room_idx].client_count--;
        // 이전 방이 비었고 일반 방이 아니면 삭제 (선택 사항)
        if (chat_rooms[old_room_idx].client_count == 0 && strcmp(chat_rooms[old_room_idx].name, "general") != 0) {
            printf("[%s][서버] 이전 방 '%s'에 더 이상 사용자가 없어 삭제합니다.\n", get_current_time_str(), chat_rooms[old_room_idx].name);
            remove_room(chat_rooms[old_room_idx].name);
        }
    }

    int new_room_idx = find_room_index(room_name);
    if (new_room_idx == -1) { // 새로운 방이면 생성
        if (add_room(room_name) != 0) {
            // 방 생성 실패 시 원래 방으로 복귀 (복잡성을 위해 여기선 간단히)
            //strcpy(clients[client_idx].room_name, "general"); // 이 줄은 제거
            return -2; // 방 생성 실패
        }
        new_room_idx = find_room_index(room_name); // 새로 생긴 방의 인덱스 다시 찾기
    }

    strncpy(clients[client_idx].room_name, room_name, MAX_ROOMNAME_LEN);
    clients[client_idx].room_name[MAX_ROOMNAME_LEN] = '\0';
    chat_rooms[new_room_idx].client_count++;

    printf("[%s][서버] 클라이언트 %s(%d)가 방 '%s'으로 이동했습니다.\n", get_current_time_str(), clients[client_idx].nickname, pid, room_name);
    return 0;
}

int leave_room(pid_t pid) {
    int client_idx = -1;
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid == pid) {
            client_idx = i;
            break;
        }
    }
    if (client_idx == -1) return -1; // 클라이언트 정보 없음
    if (strcmp(clients[client_idx].room_name, "general") == 0) {
        return -2; // 일반 방에서는 나갈 수 없음 (항상 소속)
    }

    int old_room_idx = find_room_index(clients[client_idx].room_name);
    if (old_room_idx != -1) {
        chat_rooms[old_room_idx].client_count--;
        // 이전 방이 비었고 일반 방이 아니면 삭제 (선택 사항)
        if (chat_rooms[old_room_idx].client_count == 0 && strcmp(chat_rooms[old_room_idx].name, "general") != 0) {
            printf("[%s][서버] 방 '%s'에 더 이상 사용자가 없어 삭제합니다.\n", get_current_time_str(), chat_rooms[old_room_idx].name);
            remove_room(chat_rooms[old_room_idx].name);
        }
    }

    strncpy(clients[client_idx].room_name, "general", MAX_ROOMNAME_LEN); // 기본방으로 이동
    clients[client_idx].room_name[MAX_ROOMNAME_LEN] = '\0';
    chat_rooms[find_room_index("general")].client_count++; // 일반방 사용자 수 증가

    printf("[%s][서버] 클라이언트 %s(%d)가 방을 떠나 'general' 방으로 이동했습니다.\n", get_current_time_str(), clients[client_idx].nickname, pid);
    return 0;
}

void get_room_list_message(char *buffer, size_t buf_size) {
    char temp[BUFFER_SIZE];
    snprintf(buffer, buf_size, "[%s][서버] 현재 개설된 채팅방 목록 (%d개):\n", get_current_time_str(), room_count);
    for (int i = 0; i < room_count; i++) {
        snprintf(temp, sizeof(temp), " - %s (현재 사용자: %d)\n", chat_rooms[i].name, chat_rooms[i].client_count);
        strncat(buffer, temp, buf_size - strlen(buffer) - 1);
    }
}

void get_users_in_room_message(const char *room_name, char *buffer, size_t buf_size) {
    int room_idx = find_room_index(room_name);
    if (room_idx == -1) {
        snprintf(buffer, buf_size, "[%s][서버] 방 '%s'은 존재하지 않습니다.\n", get_current_time_str(), room_name);
        return;
    }

    char temp[BUFFER_SIZE];
    snprintf(buffer, buf_size, "[%s][서버] 방 '%s'의 현재 사용자 목록 (%d명):\n", get_current_time_str(), room_name, chat_rooms[room_idx].client_count);
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].room_name, room_name) == 0) {
            snprintf(temp, sizeof(temp), " - %s\n", clients[i].nickname);
            strncat(buffer, temp, buf_size - strlen(buffer) - 1);
        }
    }
}

// ===========================================
// 메시지 전송 및 브로드캐스트 (부모 프로세스)
// ===========================================
void send_message_to_client_by_pid(pid_t target_pid, const char *message) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid == target_pid) {
            write(clients[i].pipe_write_fd, message, strlen(message));
            return;
        }
    }
    printf("[%s][서버] 메시지 전송 실패: PID %d를 가진 클라이언트를 찾을 수 없습니다.\n", get_current_time_str(), target_pid);
}

void send_message_to_client_by_nickname(const char *nickname, const char *message) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].nickname, nickname) == 0) {
            write(clients[i].pipe_write_fd, message, strlen(message));
            return;
        }
    }
    printf("[%s][서버] 메시지 전송 실패: 닉네임 '%s'를 가진 클라이언트를 찾을 수 없습니다.\n", get_current_time_str(), nickname);
}

void broadcast_message_in_room(const char *room, const char *message, pid_t sender_pid) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].room_name, room) == 0) {
            // 보낸 클라이언트에게도 다시 보냄 (선택 사항, 필요 시 sender_pid와 비교하여 제외)
            send_message_to_client_by_pid(clients[i].pid, message);
        }
    }
}

void broadcast_message_to_all_clients(const char *message, int sender_pipe_read_fd) {
    for (int i = 0; i < client_count; i++) {
        // 메시지를 보낸 자식에게는 다시 보내지 않음 (디버깅 편의를 위해 주석 처리)
        // if (clients[i].pipe_read_fd == sender_pipe_read_fd) {
        //     continue;
        // }
        write(clients[i].pipe_write_fd, message, strlen(message));
    }
}

// ===========================================
// 메시지 처리 및 라우팅 (부모 프로세스)
// ===========================================
void process_message_from_child(const char *raw_message, int sender_pipe_read_fd) {
    // 경고 해결: temp_buffer 크기 조정
    char type[BUFFER_SIZE];
    char arg1[BUFFER_SIZE];
    char arg2[BUFFER_SIZE];
    char content[BUFFER_SIZE];
    // BUFFER_SIZE (1024) * 4 로 충분히 크게 설정 (경고 메시지에 따르면 3077 바이트까지 필요할 수 있음)
    char temp_buffer[BUFFER_SIZE * 4]; 
    char client_nickname[MAX_NICKNAME_LEN + 1];
    char client_room[MAX_ROOMNAME_LEN + 1];
    pid_t sender_pid = -1;

    // 보낸 클라이언트의 정보 찾기
    for(int i = 0; i < client_count; i++) {
        if (clients[i].pipe_read_fd == sender_pipe_read_fd) {
            sender_pid = clients[i].pid;
            strncpy(client_nickname, clients[i].nickname, MAX_NICKNAME_LEN);
            client_nickname[MAX_NICKNAME_LEN] = '\0';
            strncpy(client_room, clients[i].room_name, MAX_ROOMNAME_LEN);
            client_room[MAX_ROOMNAME_LEN] = '\0';
            break;
        }
    }

    if (sender_pid == -1) {
        fprintf(stderr, "[%s][서버] 알 수 없는 발신자로부터 메시지 수신: %s\n", get_current_time_str(), raw_message);
        return;
    }

    // 메시지 파싱: [TYPE]:[ARG1]:[ARG2]:[CONTENT] 형식
    // sscanf의 반환 값으로 파싱된 필드 수를 확인하여 유효성 검사 강화
    int parsed = sscanf(raw_message, "%[^:]:%[^:]:%[^:]:%[^\n]", type, arg1, arg2, content);

    if (parsed < 2) { // 최소한 TYPE과 ARG1은 있어야 함
        snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 잘못된 메시지 형식입니다: %s\n", get_current_time_str(), raw_message);
        send_message_to_client_by_pid(sender_pid, temp_buffer);
        return;
    }

    // 각 필드의 NULL 종료 보장 (sscanf가 %[^:]를 사용할 경우 자동으로 NULL 종료되지만, 안전을 위해)
    type[sizeof(type) - 1] = '\0';
    arg1[sizeof(arg1) - 1] = '\0';
    arg2[sizeof(arg2) - 1] = '\0';
    content[sizeof(content) - 1] = '\0';


    if (strcmp(type, MSG_TYPE_CHAT) == 0) {
        // 일반 채팅 메시지: CHAT:[nickname]:[room_name]:[message]
        // 클라이언트에서 nickname과 room_name을 명시적으로 보냄
        snprintf(temp_buffer, sizeof(temp_buffer), "[%s][%s:%s] %s\n", get_current_time_str(), arg1, arg2, content);
        broadcast_message_in_room(arg2, temp_buffer, sender_pid);
    } else if (strcmp(type, MSG_TYPE_COMMAND) == 0) {
        // 서버 명령어: CMD:[command_name]:[arg]:[content] (arg와 content는 명령어에 따라 사용)
        if (strcmp(arg1, "/add") == 0) { // /add [방이름]
            int res = add_room(arg2);
            if (res == 0) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 채팅방 '%s'이(가) 생성되었습니다.\n", get_current_time_str(), arg2);
            } else if (res == -1) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 채팅방 '%s'은(는) 이미 존재합니다.\n", get_current_time_str(), arg2);
            } else { // -2
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 최대 채팅방 개수를 초과했습니다.\n", get_current_time_str());
            }
            send_message_to_client_by_pid(sender_pid, temp_buffer);
        } else if (strcmp(arg1, "/rm") == 0) { // /rm [방이름]
            int res = remove_room(arg2);
            if (res == 0) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 채팅방 '%s'이(가) 삭제되었습니다.\n", get_current_time_str(), arg2);
            } else if (res == -1) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 채팅방 '%s'은(는) 존재하지 않습니다.\n", get_current_time_str(), arg2);
            } else if (res == -2) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 채팅방 '%s'에 사용자가 남아있어 삭제할 수 없습니다.\n", get_current_time_str(), arg2);
            } else { // -3
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 기본 채팅방 'general'은 삭제할 수 없습니다.\n", get_current_time_str());
            }
            send_message_to_client_by_pid(sender_pid, temp_buffer);
        } else if (strcmp(arg1, "/join") == 0) { // /join [방이름]
            int res = join_room(sender_pid, arg2);
            if (res == 0) {
                // 이전 방에 나갔음을 알림
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][INFO] %s 님이 방을 나갔습니다.\n", get_current_time_str(), client_nickname);
                broadcast_message_in_room(client_room, temp_buffer, sender_pid); // 이전 방에 알림
                
                // 새 방에 들어왔음을 알림
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][INFO] %s 님이 방 '%s'에 입장했습니다.\n", get_current_time_str(), client_nickname, arg2);
                send_message_to_client_by_pid(sender_pid, temp_buffer); // 자신에게 입장 알림
                broadcast_message_in_room(arg2, temp_buffer, sender_pid); // 새 방에 알림
            } else if (res == -1) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 클라이언트 정보를 찾을 수 없습니다.\n", get_current_time_str());
                send_message_to_client_by_pid(sender_pid, temp_buffer);
            } else { // -2
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 방 생성 또는 입장 실패. (방 개수 초과 등)\n", get_current_time_str());
                send_message_to_client_by_pid(sender_pid, temp_buffer);
            }
        } else if (strcmp(arg1, "/leave") == 0) { // /leave
            int res = leave_room(sender_pid);
            if (res == 0) {
                 snprintf(temp_buffer, sizeof(temp_buffer), "[%s][INFO] %s 님이 방을 나갔습니다.\n", get_current_time_str(), client_nickname);
                broadcast_message_in_room(client_room, temp_buffer, sender_pid); // 이전 방에 알림

                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 방을 떠나 'general' 방으로 이동했습니다.\n", get_current_time_str());
                send_message_to_client_by_pid(sender_pid, temp_buffer);
            } else if (res == -1) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 클라이언트 정보를 찾을 수 없습니다.\n", get_current_time_str());
                send_message_to_client_by_pid(sender_pid, temp_buffer);
            } else { // -2
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 'general' 방에서는 나갈 수 없습니다.\n", get_current_time_str());
                send_message_to_client_by_pid(sender_pid, temp_buffer);
            }
        } else if (strcmp(arg1, "/list") == 0) { // /list
            get_room_list_message(temp_buffer, sizeof(temp_buffer));
            send_message_to_client_by_pid(sender_pid, temp_buffer);
        } else if (strcmp(arg1, "/users") == 0) { // /users
            get_users_in_room_message(client_room, temp_buffer, sizeof(temp_buffer));
            send_message_to_client_by_pid(sender_pid, temp_buffer);
        } else if (strcmp(arg1, "/nickname") == 0) { // /nickname [새 닉네임]
            char old_nickname[MAX_NICKNAME_LEN + 1];
            strncpy(old_nickname, client_nickname, MAX_NICKNAME_LEN);

            // 닉네임 중복 확인
            int is_duplicate = 0;
            for(int i = 0; i < client_count; i++) {
                if (clients[i].pid != sender_pid && strcmp(clients[i].nickname, arg2) == 0) {
                    is_duplicate = 1;
                    break;
                }
            }

            if (is_duplicate) {
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 오류: 닉네임 '%s'은(는) 이미 사용 중입니다.\n", get_current_time_str(), arg2);
                send_message_to_client_by_pid(sender_pid, temp_buffer);
            } else {
                for(int i = 0; i < client_count; i++) {
                    if (clients[i].pid == sender_pid) {
                        strncpy(clients[i].nickname, arg2, MAX_NICKNAME_LEN);
                        clients[i].nickname[MAX_NICKNAME_LEN] = '\0';
                        break;
                    }
                }
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 닉네임이 '%s'(으)로 변경되었습니다.\n", get_current_time_str(), arg2);
                send_message_to_client_by_pid(sender_pid, temp_buffer);

                // 방에 닉네임 변경 알림
                snprintf(temp_buffer, sizeof(temp_buffer), "[%s][INFO] %s 님이 %s (으)로 닉네임을 변경했습니다.\n", get_current_time_str(), old_nickname, arg2);
                broadcast_message_in_room(client_room, temp_buffer, sender_pid);
            }
        }
        else {
            snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 알 수 없는 명령어입니다: %s\n", get_current_time_str(), arg1);
            send_message_to_client_by_pid(sender_pid, temp_buffer);
        }
    } else if (strcmp(type, MSG_TYPE_WHISPER) == 0) {
        // 귓속말: WHISPER:[sender_nickname]:[target_nickname]:[message]
        // sender_nickname은 클라이언트가 보낸 것이고, 실제로는 서버가 sender_pid로 찾아야 안전
        snprintf(temp_buffer, sizeof(temp_buffer), "[%s][귓속말 from %s] %s\n", get_current_time_str(), client_nickname, content);
        send_message_to_client_by_nickname(arg2, temp_buffer); // arg2가 대상 닉네임

        // 보낸 사람에게도 성공 메시지 (선택 사항)
        snprintf(temp_buffer, sizeof(temp_buffer), "[%s][귓속말 to %s] %s\n", get_current_time_str(), arg2, content);
        send_message_to_client_by_pid(sender_pid, temp_buffer);

    } else {
        snprintf(temp_buffer, sizeof(temp_buffer), "[%s][서버] 알 수 없는 메시지 타입입니다: %s\n", get_current_time_str(), type);
        send_message_to_client_by_pid(sender_pid, temp_buffer);
    }
}


// ===========================================
// 자식 프로세스: 클라이언트와의 통신 처리 (fork() 이후 실행)
// ===========================================
void handle_client_child_process(int client_fd, int parent_to_child_read_fd, int child_to_parent_write_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // 클라이언트 소켓과 부모로부터의 파이프를 논블로킹으로 설정
    set_nonblocking(client_fd);
    set_nonblocking(parent_to_child_read_fd);

    printf("[%s][자식 %d] 클라이언트 핸들링 시작. FD: %d, 부모->자식 파이프 읽기 FD: %d, 자식->부모 파이프 쓰기 FD: %d\n",
           get_current_time_str(), getpid(), client_fd, parent_to_child_read_fd, child_to_parent_write_fd);

    // 자식 프로세스는 서버 리스닝 소켓을 닫음 (부모가 관리)
    // 자식 프로세스는 자신에게 필요 없는 파이프 끝을 닫음
    // 부모->자식 파이프의 쓰기 끝은 부모가 가짐, 자식은 읽기 끝만 사용
    close(parent_to_child_read_fd + 1); // pipe[1]
    // 자식->부모 파이프의 읽기 끝은 부모가 가짐, 자식은 쓰기 끝만 사용
    close(child_to_parent_write_fd - 1); // pipe[0]


    while (1) {
        // 1. 클라이언트로부터 메시지 수신 시도
        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            // 클라이언트 메시지를 부모에게 전달 (프로토콜 유지)
            write(child_to_parent_write_fd, buffer, bytes_read);
        } else if (bytes_read == 0) {
            printf("[%s][자식 %d] 클라이언트 %d 연결 종료.\n", get_current_time_str(), getpid(), client_fd);
            break; // 클라이언트 연결 종료
        } else if (bytes_read == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            perror("[%s][자식] 클라이언트 read 에러");
            break;
        }

        // 2. 부모 프로세스로부터 메시지 수신 시도
        bytes_read = read(parent_to_child_read_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            // 부모로부터 받은 메시지를 클라이언트에게 직접 전송
            write(client_fd, buffer, bytes_read);
        } else if (bytes_read == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            perror("[%s][자식] 부모 파이프 read 에러");
            break;
        }

        usleep(100); // CPU 과부하 방지를 위해 잠시 대기 (100 마이크로초)
    }

    // 자원 해제
    close(client_fd);
    close(parent_to_child_read_fd);
    close(child_to_parent_write_fd);
    printf("[%s][자식 %d] 핸들링 종료. 프로세스 종료.\n", get_current_time_str(), getpid());
    exit(EXIT_SUCCESS); // 자식 프로세스 종료
}

// ===========================================
// 부모 프로세스: 연결 수락 및 메시지 중계 (서버 메인 루프)
// ===========================================
void parent_main_loop(int server_socket) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_size;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // SIGCHLD 시그널 핸들러 등록
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // SA_RESTART: 시스템 호출 재시작, SA_NOCLDSTOP: 자식 정지 시그널 무시
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror("SIGCHLD sigaction 등록 실패");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // 서버 리스닝 소켓을 논블로킹으로 설정
    set_nonblocking(server_socket);

    // 기본 채팅방 "general" 생성
    if (add_room("general") != 0) {
        fprintf(stderr, "[%s][서버] 기본 채팅방 'general' 생성 실패.\n", get_current_time_str());
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("[%s][서버] 클라이언트 연결 대기 중...\n", get_current_time_str());

    while (1) {
        // 1. 새로운 클라이언트 연결 수락 시도 (논블로킹)
        client_addr_size = sizeof(client_addr);
        int client_fd = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_size);

        if (client_fd != -1) { // 새로운 연결이 들어옴
            if (client_count >= MAX_CLIENTS) {
                printf("[%s][서버] 최대 클라이언트 수 초과. 연결 거부: %s:%d\n",
                       get_current_time_str(), inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                char *msg = "[서버] 서버가 가득 찼습니다. 잠시 후 다시 시도해주세요.\n";
                write(client_fd, msg, strlen(msg)); // 클라이언트에게 직접 메시지 전송
                close(client_fd);
                continue;
            }

            printf("[%s][서버] 새로운 클라이언트 연결: %s:%d (FD: %d)\n",
                   get_current_time_str(), inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);

            // IPC를 위한 파이프 생성: 부모-자식 간 통신
            int parent_to_child_pipe[2]; // 부모 -> 자식 (쓰기: 부모, 읽기: 자식)
            int child_to_parent_pipe[2]; // 자식 -> 부모 (쓰기: 자식, 읽기: 부모)

            if (pipe(parent_to_child_pipe) == -1 || pipe(child_to_parent_pipe) == -1) {
                perror("파이프 생성 실패");
                close(client_fd);
                continue;
            }

            pid_t child_pid = fork(); // 자식 프로세스 생성

            if (child_pid < 0) {
                perror("fork 실패");
                close(client_fd);
                close(parent_to_child_pipe[0]); close(parent_to_child_pipe[1]);
                close(child_to_parent_pipe[0]); close(child_to_parent_pipe[1]);
                continue;
            }

            if (child_pid == 0) { // 자식 프로세스
                close(server_socket); // 자식은 서버 리스닝 소켓을 닫음
                // 자식은 자신에게 필요 없는 파이프 끝을 닫음
                close(parent_to_child_pipe[1]); // 부모->자식 파이프의 쓰기 끝 닫음
                close(child_to_parent_pipe[0]); // 자식->부모 파이프의 읽기 끝 닫음

                // 실제 클라이언트 핸들링 로직 호출
                handle_client_child_process(client_fd, parent_to_child_pipe[0], child_to_parent_pipe[1]);
                // handle_client_child_process 함수 내에서 exit(EXIT_SUCCESS) 호출됨
            } else { // 부모 프로세스
                close(client_fd); // 부모는 클라이언트 소켓을 닫음 (자식이 전담)
                // 부모는 자신에게 필요 없는 파이프 끝을 닫음
                close(parent_to_child_pipe[0]); // 부모->자식 파이프의 읽기 끝 닫음
                close(child_to_parent_pipe[1]); // 자식->부모 파이프의 쓰기 끝 닫음

                // 자식->부모 파이프 (부모가 읽는 쪽)를 논블로킹으로 설정
                set_nonblocking(child_to_parent_pipe[0]);

                // 클라이언트 정보 저장 (기본 닉네임과 방으로 시작)
                char initial_nickname[MAX_NICKNAME_LEN + 1];
                snprintf(initial_nickname, MAX_NICKNAME_LEN + 1, "user%d", child_pid);
                add_client_to_list(child_pid, child_to_parent_pipe[0], parent_to_child_pipe[1], initial_nickname, "general");

                // 새 클라이언트에게 환영 메시지 전송
                char welcome_msg[BUFFER_SIZE * 4]; // 충분한 크기 확보
                snprintf(welcome_msg, sizeof(welcome_msg), "[%s][서버] %s님, 채팅 서버에 오신 것을 환영합니다! 현재 방: %s\n",
                         get_current_time_str(), initial_nickname, "general");
                send_message_to_client_by_pid(child_pid, welcome_msg);
                
                snprintf(welcome_msg, sizeof(welcome_msg), "[%s][INFO] %s 님이 입장했습니다.\n", get_current_time_str(), initial_nickname);
                broadcast_message_in_room("general", welcome_msg, child_pid); // 기본방에 입장 알림

                printf("[%s][서버] 클라이언트 %s(%d)를 위한 자식 프로세스 %d 생성. 현재 클라이언트 수: %d\n",
                       get_current_time_str(), initial_nickname, client_fd, child_pid, client_count);
            }
        } else if (client_fd == -1 && (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            // EAGAIN/EWOULDBLOCK은 논블로킹 소켓의 일반적인 동작이므로 에러 아님
            // EINTR은 시그널에 의한 인터럽트이므로 재시도
            perror("클라이언트 연결 수락 실패");
            // 심각한 에러 발생 시 서버 종료 (여기서는 계속 진행)
        }

        // 2. 모든 자식 프로세스로부터 메시지 수신 시도 (파이프)
        for (int i = 0; i < client_count; i++) {
            bytes_read = read(clients[i].pipe_read_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                printf("[%s][부모] 자식 %d로부터 메시지 수신: %s\n", get_current_time_str(), clients[i].pid, buffer);
                // 수신된 메시지 파싱 및 처리
                process_message_from_child(buffer, clients[i].pipe_read_fd);
            } else if (bytes_read == 0) {
                // 파이프의 다른 끝 (자식 프로세스)이 닫혔음을 의미
                printf("[%s][부모] 자식 %d의 파이프 EOF 감지. 클라이언트 종료로 간주.\n", get_current_time_str(), clients[i].pid);
                // SIGCHLD 핸들러에서 remove_client_from_list가 호출되므로 여기서는 직접 호출하지 않음
                // 하지만 SIGCHLD가 오기 전에 여기서 먼저 감지될 수 있음. 이 경우 pid를 기준으로 cleanup_client_if_dead() 같은 함수 호출 고려
            } else if (bytes_read == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                perror("부모 파이프 읽기 에러");
                // 에러 발생 시 해당 자식 프로세스 처리 필요 (예: 좀비 여부 확인 후 remove_client_from_list 호출)
            }
        }

        usleep(1000); // CPU 과부하 방지를 위해 잠시 대기 (1밀리초)
    }
}

// ===========================================
// 메인 함수
// ===========================================
int main() {
    daemonize(); // 서버를 데몬 프로세스로 동작

    int server_socket;
    struct sockaddr_in server_addr;

    // 1. 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("서버 소켓 생성 실패");
        exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR 옵션 설정 (서버 재시작 시 바인딩 에러 방지)
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR 실패");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // 서버 주소 구조체 초기화
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 모든 IP 주소로부터의 연결 허용
    server_addr.sin_port = htons(PORT);       // 포트 번호 설정

    // 2. 소켓에 주소 바인딩
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("서버 바인딩 실패");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // 3. 연결 요청 대기
    if (listen(server_socket, MAX_CLIENTS) == -1) { // 최대 큐 크기
        perror("Listen 실패");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("[%s][서버] 데몬 모드로 시작됨. PORT %d에서 대기 중.\n", get_current_time_str(), PORT);

    // 부모 프로세스의 메인 루프 시작
    parent_main_loop(server_socket);

    close(server_socket); // 서버 소켓 닫기
    return 0;
}
