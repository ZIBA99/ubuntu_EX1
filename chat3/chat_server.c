#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h> // for daemonizing
#include <sys/stat.h> 

#define BUF_SIZE 1024
#define MAX_CLIENTS 30 // 최대 클라이언트 수 (필요에 따라 조정)

// 전역 변수 (자식 프로세스 관리를 위한)
// 파이프 배열: 각 자식 프로세스와 부모 프로세스 간의 통신을 위한 파이프 (부모 -> 자식)
// 클라이언트 메시지 전파를 위한 파이프 (자식 -> 부모)
// 좀비 프로세스 방지 및 자원 관리를 위한 PID 배열 등 필요

// 자식 프로세스와 통신할 파이프의 쓰기/읽기 끝을 저장할 구조체 (또는 전역 배열)
typedef struct {
    pid_t pid;
    int pipe_fd_read;  // 자식이 부모에게 메시지를 보낼 파이프의 읽기 끝
    int pipe_fd_write; // 부모가 자식에게 메시지를 보낼 파이프의 쓰기 끝
    char nickname[32]; // 클라이언트 닉네임 (나중에 추가)
    // 기타 클라이언트 관련 정보 (예: 현재 방 이름)
} client_info_t;

// 현재 연결된 클라이언트 정보를 저장할 배열 및 관련 변수
client_info_t g_clients[MAX_CLIENTS];
int g_client_count = 0;

// 부모-자식 간 통신을 위한 메인 파이프 (자식 -> 부모 메시지 수신용)
static int g_parent_pipe_read_fd; // 부모가 자식들로부터 메시지를 받을 파이프의 읽기 끝

// 함수 프로토타입
void handle_client(int client_sock, int client_idx);
void sig_chld(int signo);
void sig_usr1(int signo); // 자식으로부터 메시지 수신 시그널
void sig_int_term(int signo); // 우아한 종료 시그널 핸들러
void daemonize();
void broadcast_message(const char* message, int sender_idx); // 모든 클라이언트에게 메시지 전송

// 메인 함수
int main(int argc, char *argv[]) {
    int listen_sock, client_sock;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_addr_size;
    pid_t pid;
    int parent_to_child_pipe[2]; // 부모 -> 자식 메시지 전달용 파이프 (각 클라이언트마다 개별 파이프 필요)
    int child_to_parent_pipe[2]; // 자식 -> 부모 메시지 전달용 파이프 (각 클라이언트마다 개별 파이프 필요)
    int i;

    // 1. 데몬 프로세스화
    daemonize();

    // 2. 시그널 핸들러 설정
    // SIGCHLD: 자식 프로세스 종료 시
    signal(SIGCHLD, sig_chld);
    // SIGUSR1: 자식으로부터 메시지 수신 알림 시그널 (이 부분은 설계에 따라 달라질 수 있음)
    signal(SIGUSR1, sig_usr1);
    // SIGINT, SIGTERM: 우아한 종료
    signal(SIGINT, sig_int_term);
    signal(SIGTERM, sig_int_term);

    // 3. 소켓 생성
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket() error");
        exit(1);
    }

    // SO_REUSEADDR 옵션 설정 (포트 재사용)
    int optval = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // 4. 서버 주소 설정
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 모든 IP 주소로부터 접속 허용
    serv_addr.sin_port = htons(atoi(argv[1])); // 명령줄 인자로 포트 번호 받기

    // 5. bind()
    if (bind(listen_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind() error");
        exit(1);
    }

    // 6. listen()
    if (listen(listen_sock, 5) == -1) { // 최대 5개의 대기 연결
        perror("listen() error");
        exit(1);
    }

    fprintf(stdout, "Server started on port %s\n", argv[1]);

    // 자식 -> 부모 메시지 수신용 메인 파이프 생성 (부모 프로세스가 모든 자식으로부터 메시지를 받는 통로)
    if (pipe(child_to_parent_pipe) == -1) {
        perror("pipe() error for main communication");
        exit(1);
    }
    g_parent_pipe_read_fd = child_to_parent_pipe[0]; // 부모의 읽기 끝 저장

    // 부모의 메인 루프: 클라이언트 연결 대기 및 메시지 브로드캐스팅
    while (1) {
        // 7. accept() - 새로운 클라이언트 연결 대기
        client_addr_size = sizeof(client_addr);
        client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_sock == -1) {
            perror("accept() error");
            continue; // 에러 발생 시 다음 연결 대기
        }

        fprintf(stdout, "New client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // MAX_CLIENTS 초과 확인
        if (g_client_count >= MAX_CLIENTS) {
            fprintf(stderr, "Max clients reached. Connection rejected.\n");
            close(client_sock);
            continue;
        }

        // 각 클라이언트마다 별도의 파이프 쌍 생성 (부모 -> 자식, 자식 -> 부모)
        // 이 부분은 설계에 따라 달라질 수 있습니다.
        // 현재 뼈대에서는 자식->부모는 메인 파이프 하나, 부모->자식은 각 자식마다 하나씩 만드는 방식으로 생각.
        // 하지만 요구사항에 '브로드캐스트를 위해 pipe만 사용' 이라는 제약이 있으므로,
        // 모든 자식들이 부모에게 메시지를 보내고, 부모가 그 메시지를 받아서 모든 자식에게 다시 보내는 구조가 됩니다.
        // 따라서 각 자식과 부모 사이에 개별적인 양방향 파이프가 필요할 수 있습니다.
        // 여기서는 단순화를 위해 'child_to_parent_pipe[1]' (자식이 부모에게 쓰는 파이프)를 공유한다고 가정합니다.
        // 그리고 부모 -> 자식 메시지 전달을 위한 'parent_to_child_pipe'는 각 fork 후 생성한다고 가정합니다.

        // 자식 프로세스 생성
        pid = fork();
        if (pid == -1) {
            perror("fork() error");
            close(client_sock);
            continue;
        } else if (pid == 0) { // 자식 프로세스
            close(listen_sock); // 자식은 리슨 소켓을 닫음
            close(child_to_parent_pipe[0]); // 자식은 부모의 읽기 끝을 닫음
            g_parent_pipe_read_fd = -1; // 자식은 부모의 메인 파이프를 사용하지 않음

            // (선택 사항) 자식 -> 부모 파이프의 쓰기 끝을 g_clients 배열에 저장하지 않고,
            // 이 자식 프로세스 내에서 직접 사용하도록 설계할 수 있습니다.
            // g_client_count 등의 전역 변수는 fork 후 자식에서 변경되어도 부모에 반영되지 않습니다.
            // 따라서 handle_client 함수로 필요한 정보만 넘겨야 합니다.
            handle_client(client_sock, child_to_parent_pipe[1]); // 클라이언트 소켓과 부모에게 메시지 보낼 파이프 전달
            exit(0); // 클라이언트 처리 후 자식 프로세스 종료
        } else { // 부모 프로세스
            close(client_sock); // 부모는 클라이언트 소켓을 닫음 (자식이 처리)
            close(child_to_parent_pipe[1]); // 부모는 자식에게서 메시지 받을 것이므로 쓰기 끝을 닫음

            // 클라이언트 정보 저장 (PID와 통신용 파이프의 쓰기 끝)
            // 실제 구현에서는 각 클라이언트마다 개별 파이프 쌍을 관리해야 합니다.
            // 이 뼈대에서는 단순화하여 부모는 자식들이 모두 'child_to_parent_pipe[1]'에 쓰고,
            // 'child_to_parent_pipe[0]'으로 읽는다고 가정합니다.
            // 브로드캐스팅을 위해서는 부모가 각 자식에게 메시지를 보낼 개별 파이프가 필요합니다.
            // 이를 위해 g_clients 배열에 각 자식과 통신할 파이프의 쓰기 끝을 저장해야 합니다.

            // 현재는 간략화된 형태이며, 실제 요구사항에 맞는 파이프 구성이 필요합니다.
            // (예: pipe()를 fork 전에 MAX_CLIENTS * 2 만큼 생성하거나,
            // fork 후 각 자식마다 pipe()를 생성하고 부모가 해당 pipe의 끝을 저장하는 방식)

            // 이 예시 뼈대에서는 각 자식에게 메시지를 보낼 pipe_fd_write는 아직 생성되지 않았습니다.
            // 이는 handle_client() 에서 자식의 파이프를 생성하고, 해당 fd를 부모에게 전달하는 방식으로 구현될 수 있습니다.
            // 그러나 pipe()만 사용하고, 브로드캐스트를 위해 부모가 중앙 제어탑 역할을 한다면,
            // 부모가 모든 자식에게 메시지를 보내는 파이프가 필요합니다.
            // 가장 일반적인 접근은 부모가 각 자식 프로세스와의 통신을 위한 파이프 쌍을 각각 가지는 것입니다.

            // **핵심 변경점:** 부모-자식 간의 양방향 통신 파이프를 각 클라이언트 연결 시 생성하고 관리해야 합니다.
            // g_clients[g_client_count].pid = pid;
            // g_clients[g_client_count].pipe_fd_read = // 자식이 부모에게 보낼 파이프 읽기 끝 (부모용)
            // g_clients[g_client_count].pipe_fd_write = // 부모가 자식에게 보낼 파이프 쓰기 끝 (부모용)
            // g_client_count++;
        }
    }

    // listen_sock 닫기 (이 루프는 무한 루프이므로 사실상 도달하지 않음)
    close(listen_sock);
    return 0;
}

// 클라이언트 요청을 처리하는 자식 프로세스 함수
void handle_client(int client_sock, int child_to_parent_pipe_write_fd) {
    char buf[BUF_SIZE];
    int str_len;

    // 자식 프로세스에서 시그널 핸들러 재설정 (부모와 다르게)
    signal(SIGCHLD, SIG_DFL); // 자식은 자신의 자식을 갖지 않으므로 기본 동작
    signal(SIGUSR1, SIG_DFL); // 자식은 일반적으로 SIGUSR1을 받지 않음
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    while ((str_len = read(client_sock, buf, BUF_SIZE)) != 0) {
        // 클라이언트로부터 메시지 수신
        buf[str_len] = '\0'; // 널 종료 문자 추가
        fprintf(stdout, "[Child %d] Received from client %d: %s", getpid(), client_sock, buf);

        // 귓속말, 명령어 처리 등 (TODO: 이 부분에서 파싱 및 로직 구현)
        // 예: if (strncmp(buf, "/join ", 6) == 0) { ... }

        // 수신된 메시지를 부모에게 전달 (파이프를 통해)
        // 이 메시지를 부모가 받아서 다른 모든 자식에게 브로드캐스팅합니다.
        write(child_to_parent_pipe_write_fd, buf, str_len + 1); // 널 종료 문자까지 전송
        kill(getppid(), SIGUSR1); // 부모에게 메시지 도착 알림
    }

    // 클라이언트 연결 종료
    fprintf(stdout, "[Child %d] Client disconnected: %d\n", getpid(), client_sock);
    close(client_sock); // 클라이언트 소켓 닫기
    close(child_to_parent_pipe_write_fd); // 자식은 이 파이프의 쓰기 끝을 닫음
}

// SIGCHLD 핸들러: 좀비 프로세스 방지 
void sig_chld(int signo) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        fprintf(stdout, "Child process %d terminated.\n", pid);
        // TODO: g_clients 배열에서 해당 클라이언트 정보 제거 및 파이프 닫기
        // 제거된 자식의 PID를 관리 목록에서 제거
    }
}

// SIGUSR1 핸들러: 자식으로부터 메시지 수신 시 호출됨
void sig_usr1(int signo) {
    char buf[BUF_SIZE];
    int n;

    // TODO: g_parent_pipe_read_fd를 통해 자식들이 보낸 메시지를 읽습니다.
    // 문제는 모든 자식이 동일한 파이프 쓰기 끝을 공유하고 있으므로,
    // 이 파이프에서 읽을 때 어떤 자식이 보낸 메시지인지 구분하기 어렵습니다.
    // 따라서 실제 구현에서는 각 자식마다 부모에게 메시지를 보낼 별도의 파이프가 필요합니다.
    // 이 예시 뼈대에서는 단순히 메시지를 읽어 브로드캐스팅하는 방식으로 가정합니다.

    // 이 예시에서는 모든 자식이 g_parent_pipe_read_fd의 쓰기 끝에 메시지를 보냅니다.
    // 실제 구현에서는 각 클라이언트의 파이프를 select/poll 없이 어떻게 감지할지 중요합니다.
    // 제약사항(멀티플렉싱 함수 금지) 때문에 SIGUSR1을 통한 비동기 알림 방식이 핵심입니다.
    // 즉, 메시지를 보낸 자식이 부모에게 SIGUSR1을 보내면, 부모는 '어딘가에서' 메시지가 왔음을 인지하고
    // 자신의 모든 자식 파이프를 순회하며 읽을 데이터가 있는지 확인해야 합니다.
    // 하지만 파이프에 데이터가 있는지 확인하는 것은 ioctl(fd, FIONREAD, &bytes_avail)로 가능하지만,
    // 이 또한 특정 형태의 I/O 멀티플렉싱으로 간주될 여지가 있으므로 주의해야 합니다.
    // 가장 순수한 방법은 부모가 메시지 수신 파이프에서 블로킹으로 기다리거나,
    // 각 자식이 메시지를 보낼 때마다 부모에게 SIGUSR1을 보내고, 부모는 시그널 핸들러에서 메시지를 처리하는 방식입니다.

    // 임시 처리: 모든 자식이 하나의 파이프로 메시지를 보내고 부모가 그 파이프에서 읽는다고 가정.
    // 실제 구현에서는 각 자식으로부터의 메시지를 구분하는 더 복잡한 로직이 필요.
    if ((n = read(g_parent_pipe_read_fd, buf, BUF_SIZE)) > 0) {
        buf[n] = '\0';
        fprintf(stdout, "[Parent] Received message for broadcast: %s", buf);
        broadcast_message(buf, -1); // -1은 보낸 사람 구분을 위한 임시 값 (실제 구현 시 필요)
    }
}

// SIGINT, SIGTERM 핸들러: 서버 종료 시 모든 자식 프로세스 종료 및 자원 정리
void sig_int_term(int signo) {
    fprintf(stdout, "Server is shutting down...\n");
    // TODO: 모든 자식 프로세스에게 SIGTERM 등을 보내 종료를 알리고 waitpid로 기다립니다.
    // 사용 중이던 IPC 자원(파이프)도 깨끗하게 정리
    for (int i = 0; i < g_client_count; i++) {
        if (g_clients[i].pid > 0) {
            kill(g_clients[i].pid, SIGTERM); // 자식에게 종료 시그널 전송
            close(g_clients[i].pipe_fd_read);
            close(g_clients[i].pipe_fd_write);
        }
    }
    // 모든 자식이 종료될 때까지 기다림 (선택 사항, sig_chld가 처리할 수도 있음)
    while (wait(NULL) > 0);
    close(g_parent_pipe_read_fd); // 부모의 메인 파이프도 닫음
    exit(0);
}

// 데몬 프로세스화 함수
void daemonize() {
    pid_t pid;

    // 1. fork()
    pid = fork();
    if (pid < 0) {
        perror("daemonize: fork error");
        exit(1);
    }
    if (pid > 0) { // 부모 프로세스 종료
        exit(0);
    }

    // 2. 새로운 세션 시작
    if (setsid() < 0) {
        perror("daemonize: setsid error");
        exit(1);
    }

    // 3. 다시 fork() (제어 터미널 재획득 방지)
    signal(SIGHUP, SIG_IGN); // 터미널 종료 시그널 무시
    pid = fork();
    if (pid < 0) {
        perror("daemonize: second fork error");
        exit(1);
    }
    if (pid > 0) { // 두 번째 부모 프로세스 종료
        exit(0);
    }

    // 4. 작업 디렉토리 변경 (옵션)
    // chdir("/");

    // 5. 파일 모드 생성 마스크 변경 (umask)
    umask(0);

    // 6. 표준 입출력 닫기 (데몬이므로)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // /dev/null로 리다이렉트 (옵션)
    // open("/dev/null", O_RDWR);
    // dup(0);
    // dup(0);
}

// 모든 클라이언트에게 메시지를 브로드캐스팅하는 함수
// TODO: 이 함수는 부모 프로세스에서만 호출되어야 하며,
// 각 클라이언트에게 연결된 파이프의 쓰기 끝을 통해 메시지를 전송해야 합니다.
void broadcast_message(const char* message, int sender_idx) {
    // 메시지 수신 파이프에서 메시지를 받은 후,
    // g_clients 배열에 저장된 각 클라이언트의 파이프 쓰기 끝(pipe_fd_write)으로 메시지를 전송합니다.
    for (int i = 0; i < g_client_count; i++) {
        // 자신을 제외한 모든 클라이언트에게 (sender_idx가 유효하다면)
        // 실제 구현에서는 방 개념이 있으므로 해당 방의 클라이언트에게만 전송
        // write(g_clients[i].pipe_fd_write, message, strlen(message) + 1);
        // kill(g_clients[i].pid, SIGUSR1_FOR_MESSAGE_DELIVERY_TO_CHILD); // 자식에게 메시지 도착 알림 시그널
    }
}
