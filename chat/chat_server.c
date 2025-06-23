// chat_server.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>  // umask()

#define PORT 12345
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024
#define NICKNAME_SIZE 32
#define MAX_ROOMS 10
#define MAX_USERS_PER_ROOM 10
#define ROOM_NAME_SIZE 32

typedef struct {
    char name[ROOM_NAME_SIZE];
    pid_t users[MAX_USERS_PER_ROOM];
    int user_count;
} ChatRoom;

typedef struct {
    pid_t pid;
    int pipe_parent[2]; // 자식→부모
    int pipe_child[2];  // 부모→자식
    char nickname[NICKNAME_SIZE];
    char current_room[ROOM_NAME_SIZE];
} ClientProcess;

ClientProcess clients[MAX_CLIENTS];
int client_count = 0;
ChatRoom rooms[MAX_ROOMS];
int room_count = 0;
int server_socket;

// ========== 데몬화 ==========
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

// ========== 클라이언트에 메시지 전송 ==========
void send_to_client(pid_t pid, const char* msg) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].pid == pid) {
            write(clients[i].pipe_child[1], msg, strlen(msg));
            break;
        }
    }
}

// ========== 명령어 처리 ==========
void handle_command(pid_t sender_pid, const char* msg) {
    char command[BUFFER_SIZE];
    strncpy(command, msg, BUFFER_SIZE);
    char* token = strtok(command, " \n");

    if (!token) return;

    if (strcmp(token, "/add") == 0) {
        char* room_name = strtok(NULL, " \n");
        if (!room_name) return;
        for (int i = 0; i < room_count; i++) {
            if (strcmp(rooms[i].name, room_name) == 0) return;
        }
        strncpy(rooms[room_count].name, room_name, ROOM_NAME_SIZE);
        rooms[room_count].user_count = 0;
        room_count++;
        send_to_client(sender_pid, "방이 생성되었습니다.\n");

    } else if (strcmp(token, "/join") == 0) {
        char* room_name = strtok(NULL, " \n");
        if (!room_name) return;
        int room_idx = -1;
        for (int i = 0; i < room_count; i++) {
            if (strcmp(rooms[i].name, room_name) == 0)
                room_idx = i;
        }
        if (room_idx == -1) {
            send_to_client(sender_pid, "해당 방이 존재하지 않습니다.\n");
            return;
        }
        for (int i = 0; i < client_count; i++) {
            if (clients[i].pid == sender_pid) {
                strcpy(clients[i].current_room, room_name);
                rooms[room_idx].users[rooms[room_idx].user_count++] = sender_pid;
                break;
            }
        }
        send_to_client(sender_pid, "방에 입장했습니다.\n");

    } else if (strcmp(token, "/list") == 0) {
        char list[BUFFER_SIZE] = "방 목록:\n";
        for (int i = 0; i < room_count; i++) {
            strcat(list, "- ");
            strcat(list, rooms[i].name);
            strcat(list, "\n");
        }
        send_to_client(sender_pid, list);

    } else if (strcmp(token, "/users") == 0) {
        char room[ROOM_NAME_SIZE] = "";
        for (int i = 0; i < client_count; i++) {
            if (clients[i].pid == sender_pid) {
                strcpy(room, clients[i].current_room);
                break;
            }
        }
        char list[BUFFER_SIZE] = "현재 방 사용자:\n";
        for (int i = 0; i < room_count; i++) {
            if (strcmp(rooms[i].name, room) == 0) {
                for (int j = 0; j < rooms[i].user_count; j++) {
                    pid_t uid = rooms[i].users[j];
                    for (int k = 0; k < client_count; k++) {
                        if (clients[k].pid == uid) {
                            strcat(list, "- ");
                            strcat(list, clients[k].nickname);
                            strcat(list, "\n");
                        }
                    }
                }
            }
        }
        send_to_client(sender_pid, list);

    } else if (strcmp(token, "/leave") == 0) {
        for (int i = 0; i < client_count; i++) {
            if (clients[i].pid == sender_pid) {
                for (int j = 0; j < room_count; j++) {
                    if (strcmp(rooms[j].name, clients[i].current_room) == 0) {
                        for (int u = 0; u < rooms[j].user_count; u++) {
                            if (rooms[j].users[u] == sender_pid) {
                                rooms[j].users[u] = rooms[j].users[--rooms[j].user_count];
                                break;
                            }
                        }
                        clients[i].current_room[0] = '\0';
                        send_to_client(sender_pid, "방에서 나갔습니다.\n");
                        return;
                    }
                }
            }
        }

    } else if (strcmp(token, "/rm") == 0) {
        char* room_name = strtok(NULL, " \n");
        if (!room_name) return;
        for (int i = 0; i < room_count; i++) {
            if (strcmp(rooms[i].name, room_name) == 0) {
                if (rooms[i].user_count > 0) {
                    send_to_client(sender_pid, "사용자가 있어 삭제 불가\n");
                    return;
                }
                rooms[i] = rooms[--room_count];
                send_to_client(sender_pid, "방을 삭제했습니다.\n");
                return;
            }
        }
        send_to_client(sender_pid, "해당 방이 없습니다.\n");
    }
}

// ========== 시그널 처리 ==========
void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < client_count; i++) {
            if (clients[i].pid == pid) {
                close(clients[i].pipe_parent[0]);
                close(clients[i].pipe_child[1]);
                clients[i] = clients[--client_count];
                break;
            }
        }
    }
}

void sigusr1_handler(int sig) {
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < client_count; i++) {
        int len = read(clients[i].pipe_parent[0], buffer, BUFFER_SIZE - 1);
        if (len > 0) {
            buffer[len] = '\0';
            if (buffer[0] == '/') {
                handle_command(clients[i].pid, buffer);
            } else {
                char msg[1200];
                snprintf(msg, sizeof(msg) - 1, "[%s] %s", clients[i].nickname, buffer);
                msg[sizeof(msg) - 1] = '\0';
                for (int j = 0; j < client_count; j++) {
                    if (strcmp(clients[j].current_room, clients[i].current_room) == 0)
                        write(clients[j].pipe_child[1], msg, strlen(msg));
                }
            }
        }
    }
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);
}

// ========== 메인 ==========
int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);

    daemonize();
    setup_signal_handlers();

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);

    while (1) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addrlen);
        if (client_socket < 0) continue;
        if (client_count >= MAX_CLIENTS) {
            close(client_socket);
            continue;
        }

        int p2c[2], c2p[2];
        pipe(p2c);
        pipe(c2p);

        pid_t pid = fork();
        if (pid == 0) {
            close(server_socket);
            close(p2c[1]); close(c2p[0]);

            char nickname[NICKNAME_SIZE];
            read(client_socket, nickname, NICKNAME_SIZE);

            char buffer[BUFFER_SIZE];
            while (1) {
                int n = read(client_socket, buffer, BUFFER_SIZE);
                if (n <= 0) break;
                buffer[n] = '\0';
                write(c2p[1], buffer, n);
                kill(getppid(), SIGUSR1);

                int m = read(p2c[0], buffer, BUFFER_SIZE);
                if (m > 0) write(client_socket, buffer, m);
            }
            exit(0);
        } else {
            close(client_socket);
            close(p2c[0]); close(c2p[1]);

            clients[client_count].pid = pid;
            clients[client_count].pipe_child[1] = p2c[1];
            clients[client_count].pipe_parent[0] = c2p[0];
            read(client_socket, clients[client_count].nickname, NICKNAME_SIZE);
            clients[client_count].current_room[0] = '\0';
            client_count++;
        }
    }
    return 0;
}

