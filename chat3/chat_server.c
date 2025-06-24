#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h> // for errno and perror

// 최대 클라이언트 수 (동시에 처리할 수 있는 연결)
#define MAX_CLIENTS 10
// 버퍼 크기
#define BUF_SIZE 1024

// 연결된 클라이언트 소켓 파일 디스크립터를 저장하는 배열
// 이 배열은 모든 자식 프로세스에서 복사본을 가지게 되지만,
// 실제 브로드캐스트를 위해서는 부모가 관리하거나, IPC (파이프, 메시지 큐 등)를 통해 자식들이 공유해야 합니다.
// 여기서는 간단하게 각 자식 프로세스가 개별적으로 처리하도록 구현합니다.
// 즉, 각 자식은 자신이 담당하는 클라이언트와만 통신하며, 다른 클라이언트에게는 메시지를 전달하지 않습니다.
// 진정한 "채팅 서버"를 만들려면 이 부분에 대한 IPC (예: 소켓 배열을 부모가 관리하며, 자식은 메시지를 부모에게 보내고 부모가 브로드캐스트)가 필요합니다.
// 이 예제는 fork() 기반 서버의 기본 동작을 보여주는 데 중점을 둡니다.

// 클라이언트 소켓 배열 (전역 변수로 선언하여 시그널 핸들러에서도 접근 가능하게 함)
// volatile 키워드는 최적화를 방지하고 변수가 외부에서 변경될 수 있음을 컴파일러에게 알립니다.
volatile int client_socks[MAX_CLIENTS];
volatile int client_count = 0; // 현재 연결된 클라이언트 수

// 자식 프로세스 종료 시 좀비 프로세스를 방지하기 위한 시그널 핸들러
void handle_child_exit(int sig) {
    int status;
    pid_t child_pid;
    // WNOHANG: 자식이 종료되지 않았어도 바로 리턴 (블로킹 방지)
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[SERVER] Child process %d terminated.\n", child_pid);
        // 클라이언트 소켓 배열에서 해당 클라이언트 제거 로직 (선택 사항)
        // 실제 채팅 서버에서는 이 부분에서 client_socks 배열에서 해당 소켓을 제거하고 관리해야 합니다.
    }
}

// 연결된 모든 클라이언트에게 메시지를 브로드캐스트하는 함수
// 이 함수는 '부모' 프로세스에서 사용될 수 있지만,
// 각 '자식' 프로세스가 독립적으로 동작하는 현재 모델에서는
// 각 자식 프로세스가 자신이 담당하는 클라이언트에게만 메시지를 보낼 수 있습니다.
// 진정한 브로드캐스트를 위해서는 IPC (예: 부모가 메시지 중계)가 필요합니다.
void broadcast_message(const char *message, int sender_sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_socks[i] != 0 && client_socks[i] != sender_sock) { // 자신을 제외한 모든 클라이언트에게
            if (send(client_socks[i], message, strlen(message), 0) < 0) {
                perror("send to client failed");
                // TODO: 에러 발생 시 해당 클라이언트 연결 끊기 처리
            }
        }
    }
}

// 클라이언트와의 통신을 처리하는 자식 프로세스 함수
void handle_client(int client_sock, int client_idx) {
    char buffer[BUF_SIZE];
    int str_len;

    // 클라이언트 소켓을 client_socks 배열에 추가 (자식 프로세스 내부에서)
    // 이 배열은 각 자식 프로세스마다 독립적인 복사본을 가지므로, 브로드캐스트는 이 배열만으로는 불가능합니다.
    // 진정한 브로드캐스트를 위해서는 부모-자식 간의 소켓 파일 디스크립터 공유 또는 IPC가 필요합니다.
    client_socks[client_idx] = client_sock; // 이 부분은 실제로는 의미가 없음 (부모의 배열에 반영 안됨)

    printf("[SERVER][PID %d] Client %d connected (socket %d).\n", getpid(), client_idx, client_sock);

    while (1) {
        memset(buffer, 0, sizeof(buffer)); // 버퍼 초기화
        str_len = recv(client_sock, buffer, BUF_SIZE, 0); // 클라이언트로부터 메시지 수신

        if (str_len == 0) { // 클라이언트 연결 종료
            printf("[SERVER][PID %d] Client %d disconnected (socket %d).\n", getpid(), client_idx, client_sock);
            break;
        } else if (str_len < 0) { // 메시지 수신 에러
            if (errno == EINTR) continue; // 인터럽트 시그널 발생 시 재시도
            perror("recv error");
            break;
        }

        // 수신된 메시지 출력
        printf("[SERVER][PID %d][Client %d] Received: %s", getpid(), client_idx, buffer);

        // --- 여기서부터는 실제 채팅 서버 로직 (브로드캐스트) ---
        // 현재 이 자식 프로세스 모델에서는 메시지를 자신이 담당하는 클라이언트에게만 다시 보내거나,
        // 부모 프로세스에게 메시지를 전달하여 부모가 모든 클라이언트에게 브로드캐스트하는 방식으로 구현해야 합니다.
        // 아래 코드는 단순한 에코 서버처럼 동작합니다. (받은 메시지를 다시 보냄)
        // 진정한 채팅 서버 구현을 위한 주석 처리:
        // broadcast_message(buffer, client_sock); // 이 함수는 현재 구조에서 제대로 작동하지 않음

        // 임시로, 받은 메시지를 모든 클라이언트에게 다시 전송 (현재는 각 자식 프로세스가 독립적으로 처리)
        // 이 부분을 실제 채팅 서버처럼 구현하려면 복잡한 IPC가 필요합니다.
        // 예를 들어, 자식들이 메시지를 부모에게 보내고, 부모가 모든 연결된 클라이언트 FD를 관리하며 메시지를 브로드캐스트하는 방식.
        // 아니면, 각 자식이 다른 자식들과 통신하여 메시지를 전달하는 방식 (더 복잡)
        // 여기서는 간단하게 "에코"처럼 작동하도록 하겠습니다.
        if (send(client_sock, buffer, str_len, 0) < 0) {
            perror("send echo failed");
            break;
        }

        // 클라이언트가 "quit\n"을 보내면 종료
        if (strcmp(buffer, "quit\n") == 0 || strcmp(buffer, "quit\r\n") == 0) { // 윈도우 클라이언트 고려
            printf("[SERVER][PID %d] Client %d requested quit.\n", getpid(), client_idx);
            break;
        }
    }

    // 클라이언트 소켓 닫기
    close(client_sock);
    // client_socks 배열에서 해당 클라이언트 제거 (현재 자식 프로세스에서는 의미 없음, 부모가 관리해야 함)
    // client_socks[client_idx] = 0;
    exit(0); // 자식 프로세스 종료
}

int main(int argc, char *argv[]) {
    int server_sock;           // 서버 소켓 파일 디스크립터
    int client_sock;           // 클라이언트 소켓 파일 디스크립터
    struct sockaddr_in server_addr; // 서버 주소 구조체
    struct sockaddr_in client_addr; // 클라이언트 주소 구조체
    socklen_t client_addr_size; // 클라이언트 주소 크기
    int port_num;              // 포트 번호
    pid_t pid;                 // 자식 프로세스 ID

    // 시그널 핸들러 등록: 자식 프로세스가 종료될 때 SIGCHLD 시그널을 받아서 좀비 프로세스 방지
    signal(SIGCHLD, handle_child_exit);

    // 인자 확인 (포트 번호)
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port_num = atoi(argv[1]); // 포트 번호 문자열을 정수로 변환

    // 1. 서버 소켓 생성 (TCP)
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("socket creation failed");
        exit(1);
    }

    // 2. 서버 주소 구조체 설정
    memset(&server_addr, 0, sizeof(server_addr)); // 구조체 0으로 초기화
    server_addr.sin_family = AF_INET;             // IPv4 주소 체계
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 모든 네트워크 인터페이스로부터의 연결 허용
    server_addr.sin_port = htons(port_num);       // 포트 번호 설정 (네트워크 바이트 순서로 변환)

    // 3. SO_REUSEADDR 옵션 설정 (선택 사항):
    // 서버 종료 후 재시작 시, TIME_WAIT 상태의 포트를 재사용할 수 있도록 함
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        // 이 에러는 치명적이지 않을 수 있으므로 exit 대신 경고만 할 수도 있습니다.
    }

    // 4. 소켓에 주소 바인딩
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(server_sock);
        exit(1);
    }

    // 5. 클라이언트 연결 요청 대기 (큐 크기: 5)
    if (listen(server_sock, 5) == -1) {
        perror("listen failed");
        close(server_sock);
        exit(1);
    }

    printf("[SERVER] Chat server started on port %d.\n", port_num);
    printf("[SERVER] Waiting for connections...\n");

    int client_idx = 0; // 클라이언트 인덱스 (단순 식별용)

    while (1) {
        client_addr_size = sizeof(client_addr);
        // 6. 클라이언트 연결 수락
        // accept()는 클라이언트 연결 요청이 올 때까지 블로킹됨
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_size);
        if (client_sock == -1) {
            if (errno == EINTR) { // accept가 시그널에 의해 중단된 경우 재시도
                continue;
            }
            perror("accept failed");
            // 서버는 계속 실행되어야 하므로 exit 대신 경고만 하고 다음 연결을 기다릴 수 있습니다.
            continue;
        }

        // 새로운 클라이언트 연결 시도 알림
        printf("[SERVER] New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // 7. 클라이언트별로 자식 프로세스 생성
        pid = fork();
        if (pid < 0) { // fork 실패
            perror("fork failed");
            close(client_sock); // 연결된 클라이언트 소켓 닫고 다음 연결 대기
            continue; // 서버는 계속 실행
        } else if (pid == 0) { // 자식 프로세스 (새로운 클라이언트와 통신 전담)
            close(server_sock); // 자식은 서버 소켓을 닫음 (더 이상 새로운 연결을 수락할 필요 없음)
            handle_client(client_sock, client_idx); // 클라이언트 통신 처리 함수 호출
            // handle_client 함수 내에서 exit(0) 호출로 자식 프로세스 종료
        } else { // 부모 프로세스
            close(client_sock); // 부모는 클라이언트 소켓을 닫음 (자식이 처리할 것이므로)
            // 클라이언트 인덱스 증가 (실제 사용 안 됨, 클라이언트 관리는 복잡함)
            client_idx++;
            // client_socks 배열은 부모와 자식이 각각의 복사본을 가집니다.
            // 따라서 여기서 client_socks[client_idx] = client_sock; 하는 것은 자식 프로세스의 배열에 영향을 주지 않습니다.
            // 진정한 브로드캐스트를 위해서는 부모가 모든 클라이언트의 소켓 FD를 관리해야 합니다.
        }
    }

    // 서버 소켓 닫기 (이 코드는 무한 루프이므로 실제로 실행될 가능성은 낮습니다. Ctrl+C로 종료될 때)
    close(server_sock);
    return 0;
}
