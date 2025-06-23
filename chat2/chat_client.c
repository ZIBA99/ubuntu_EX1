#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h> // poll 함수를 사용하기 위해
#include <time.h> // 시간 기록을 위해

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_NICKNAME_LEN 31
#define MAX_ROOMNAME_LEN 31

// 메시지 타입 정의 (서버와 동일하게 클라이언트에서도 정의)
#define MSG_TYPE_CHAT       "CHAT"      // 일반 채팅 메시지
#define MSG_TYPE_COMMAND    "CMD"       // 서버 명령어
#define MSG_TYPE_WHISPER    "WHISPER"   // 귓속말
#define MSG_TYPE_JOIN       "JOIN"      // 채팅방 입장 알림
#define MSG_TYPE_LEAVE      "LEAVE"     // 채팅방 퇴장 알림
#define MSG_TYPE_INFO       "INFO"      // 서버 정보 메시지 (예: 명령어 결과, 오류)


char current_nickname[MAX_NICKNAME_LEN + 1];
char current_room[MAX_ROOMNAME_LEN + 1];

// 유틸리티 함수: 현재 시간 문자열 반환
char* get_current_time_str() {
    static char time_str[30];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
    return time_str;
}

void display_help() {
    printf("\n[%s][도움말] 사용 가능한 명령어:\n", get_current_time_str());
    printf("  /nickname [새닉네임]     : 닉네임 변경\n");
    printf("  /add [방이름]            : 새 채팅방 생성\n");
    printf("  /rm [방이름]             : 채팅방 삭제 (방에 아무도 없을 때만 가능, 'general' 제외)\n");
    printf("  /join [방이름]           : 채팅방 입장\n");
    printf("  /leave                   : 현재 방을 떠나 'general' 방으로 이동\n");
    printf("  /list                    : 전체 채팅방 목록 조회\n");
    printf("  /users                   : 현재 방 사용자 목록 조회\n");
    printf("  !whisper [상대방닉네임] [메시지] : 특정 사용자에게 귓속말 전송\n");
    printf("  /help                    : 도움말 표시\n");
    printf("  /quit 또는 /exit         : 채팅 종료\n");
    printf("\n");
}


int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char formatted_message[BUFFER_SIZE * 2]; // 포맷된 메시지 (타입, 닉네임, 방이름, 내용 등 포함)
    ssize_t bytes_sent, bytes_received;

    // 닉네임 초기화 (임시, 서버에서 초기 닉네임 부여할 수 있음)
    strncpy(current_nickname, "guest", MAX_NICKNAME_LEN);
    current_nickname[MAX_NICKNAME_LEN] = '\0';
    strncpy(current_room, "general", MAX_ROOMNAME_LEN);
    current_room[MAX_ROOMNAME_LEN] = '\0';

    // 1. 소켓 생성
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("클라이언트 소켓 생성 실패");
        exit(EXIT_FAILURE);
    }

    // 서버 주소 설정
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("유효하지 않은 서버 주소/주소 변환 실패");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    // 2. 서버에 연결
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("서버 연결 실패");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    printf("[%s][클라이언트] 서버에 연결되었습니다 (%s:%d).\n", get_current_time_str(), SERVER_IP, PORT);
    display_help(); // 연결 후 도움말 표시

    // pollfd 구조체 배열 설정
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO; // 표준 입력
    fds[0].events = POLLIN;
    fds[1].fd = client_socket; // 서버 소켓
    fds[1].events = POLLIN;

    while (1) {
        int poll_count = poll(fds, 2, -1); // 무한 대기
        if (poll_count == -1) {
            perror("poll 에러");
            break;
        }

        // 1. 표준 입력 (사용자 입력) 처리
        if (fds[0].revents & POLLIN) {
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                // Ctrl+D 입력 처리 또는 에러
                printf("[%s][클라이언트] 입력 종료. 클라이언트 종료.\n", get_current_time_str());
                break;
            }
            buffer[strcspn(buffer, "\n")] = '\0'; // 개행 문자 제거

            if (strlen(buffer) == 0) {
                continue; // 빈 입력 무시
            }

            char raw_input[BUFFER_SIZE];
            strncpy(raw_input, buffer, sizeof(raw_input) - 1);
            raw_input[sizeof(raw_input) - 1] = '\0'; // NULL 종료 보장

            // 명령어 처리
            if (raw_input[0] == '/') {
                if (strcmp(raw_input, "/quit") == 0 || strcmp(raw_input, "/exit") == 0) {
                    printf("[%s][클라이언트] 채팅을 종료합니다.\n", get_current_time_str());
                    break;
                } else if (strcmp(raw_input, "/help") == 0) {
                    display_help();
                } else if (strncmp(raw_input, "/nickname ", 10) == 0) {
                    char new_nickname[MAX_NICKNAME_LEN + 1];
                    sscanf(raw_input + 10, "%s", new_nickname);
                    if (strlen(new_nickname) > MAX_NICKNAME_LEN) {
                        printf("[%s][클라이언트] 오류: 닉네임은 최대 %d자까지 가능합니다.\n", get_current_time_str(), MAX_NICKNAME_LEN);
                        continue;
                    }
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "nickname", new_nickname, "");
                } else if (strncmp(raw_input, "/add ", 5) == 0) {
                    char room_name[MAX_ROOMNAME_LEN + 1];
                    sscanf(raw_input + 5, "%s", room_name);
                     if (strlen(room_name) > MAX_ROOMNAME_LEN) {
                        printf("[%s][클라이언트] 오류: 방 이름은 최대 %d자까지 가능합니다.\n", get_current_time_str(), MAX_ROOMNAME_LEN);
                        continue;
                    }
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "add", room_name, "");
                } else if (strncmp(raw_input, "/rm ", 4) == 0) {
                    char room_name[MAX_ROOMNAME_LEN + 1];
                    sscanf(raw_input + 4, "%s", room_name);
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "rm", room_name, "");
                } else if (strncmp(raw_input, "/join ", 6) == 0) {
                    char room_name[MAX_ROOMNAME_LEN + 1];
                    sscanf(raw_input + 6, "%s", room_name);
                     if (strlen(room_name) > MAX_ROOMNAME_LEN) {
                        printf("[%s][클라이언트] 오류: 방 이름은 최대 %d자까지 가능합니다.\n", get_current_time_str(), MAX_ROOMNAME_LEN);
                        continue;
                    }
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "join", room_name, "");
                } else if (strcmp(raw_input, "/leave") == 0) {
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "leave", "", "");
                } else if (strcmp(raw_input, "/list") == 0) {
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "list", "", "");
                } else if (strcmp(raw_input, "/users") == 0) {
                    snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_COMMAND, "users", "", "");
                } else {
                    printf("[%s][클라이언트] 알 수 없는 명령어: %s\n", get_current_time_str(), raw_input);
                    continue; // 서버로 전송하지 않음
                }
            } else if (strncmp(raw_input, "!whisper ", 9) == 0) {
                char target_nickname[MAX_NICKNAME_LEN + 1];
                char whisper_content[BUFFER_SIZE];
                
                // !whisper [상대방 닉네임] [메시지] 파싱
                // sscanf 대신 sscanf_s (Windows) 또는 더 견고한 파싱 (Linux) 필요
                // 여기서는 간단하게 공백으로 구분하여 파싱
                char *token = strtok(raw_input + 9, " ");
                if (token != NULL) {
                    strncpy(target_nickname, token, MAX_NICKNAME_LEN);
                    target_nickname[MAX_NICKNAME_LEN] = '\0';

                    char *content_start = raw_input + 9 + strlen(token) + 1; // 닉네임 뒤의 공백까지 건너뛰기
                    if (strlen(content_start) > 0) {
                        strncpy(whisper_content, content_start, BUFFER_SIZE - 1);
                        whisper_content[BUFFER_SIZE - 1] = '\0';
                        snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_WHISPER, current_nickname, target_nickname, whisper_content);
                    } else {
                        printf("[%s][클라이언트] 오류: 귓속말 내용이 비어 있습니다. 사용법: !whisper [상대방닉네임] [메시지]\n", get_current_time_str());
                        continue;
                    }
                } else {
                    printf("[%s][클라이언트] 오류: 귓속말 대상 닉네임이 지정되지 않았습니다. 사용법: !whisper [상대방닉네임] [메시지]\n", get_current_time_str());
                    continue;
                }

            }
            else {
                // 일반 채팅 메시지
                snprintf(formatted_message, sizeof(formatted_message), "%s:%s:%s:%s", MSG_TYPE_CHAT, current_nickname, current_room, raw_input);
            }

            // 서버로 메시지 전송
            bytes_sent = write(client_socket, formatted_message, strlen(formatted_message));
            if (bytes_sent == -1) {
                perror("메시지 전송 실패");
                break;
            }
        }

        // 2. 서버로부터 메시지 수신 처리
        if (fds[1].revents & POLLIN) {
            bytes_received = read(client_socket, buffer, sizeof(buffer) - 1);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                printf("%s", buffer); // 서버에서 보낸 메시지를 그대로 출력

                // 서버 응답을 기반으로 클라이언트 상태 업데이트 (닉네임, 방 이름 등)
                // 예: [서버] 닉네임이 '새닉네임'(으)로 변경되었습니다.
                if (strstr(buffer, "[서버] 닉네임이 '") != NULL && strstr(buffer, "'(으)로 변경되었습니다.") != NULL) {
                    char *start = strstr(buffer, "[서버] 닉네임이 '") + strlen("[서버] 닉네임이 '");
                    char *end = strstr(start, "'(으)로 변경되었습니다.");
                    if (start && end) {
                        *end = '\0'; // 닉네임 부분만 남기도록 NULL 종료
                        strncpy(current_nickname, start, MAX_NICKNAME_LEN);
                        current_nickname[MAX_NICKNAME_LEN] = '\0';
                    }
                } else if (strstr(buffer, "[INFO] ") != NULL && strstr(buffer, " 님이 방 '") != NULL && strstr(buffer, "'에 입장했습니다.\n") != NULL) {
                     char *start = strstr(buffer, " 님이 방 '") + strlen(" 님이 방 '");
                     char *end = strstr(start, "'에 입장했습니다.\n");
                     // 현재 클라이언트가 보낸 입장 메시지인 경우에만 룸 이름 업데이트
                     // (다른 유저의 입장 메시지와 구분하기 위해 추가적인 로직 필요할 수 있음)
                     // 여기서는 단순히 'general'이 아닌 방으로 입장 시 업데이트
                     if (start && end) {
                        char entered_room[MAX_ROOMNAME_LEN + 1];
                        size_t len = end - start;
                        if (len < MAX_ROOMNAME_LEN + 1) {
                            strncpy(entered_room, start, len);
                            entered_room[len] = '\0';
                            if (strcmp(entered_room, "general") != 0 && strcmp(entered_room, current_room) != 0) { // 이미 같은 방이 아니면 업데이트
                                strncpy(current_room, entered_room, MAX_ROOMNAME_LEN);
                                current_room[MAX_ROOMNAME_LEN] = '\0';
                            }
                        }
                    }
                } else if (strstr(buffer, "[서버] 방을 떠나 'general' 방으로 이동했습니다.\n") != NULL) {
                    strncpy(current_room, "general", MAX_ROOMNAME_LEN);
                    current_room[MAX_ROOMNAME_LEN] = '\0';
                }

            } else if (bytes_received == 0) {
                printf("[%s][클라이언트] 서버 연결이 종료되었습니다.\n", get_current_time_str());
                break; // 서버 연결 종료
            } else {
                perror("서버로부터 메시지 수신 실패");
                break;
            }
        }
    }

    close(client_socket);
    return 0;
}
