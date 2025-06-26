#include <stdio.h>
#include <string.h>
#include <unistd.h> 			// For STDOUT_FILENO
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h> // for errno

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

// BUFSIZ는 stdio.h에 정의되어 있으며, 일반적으로 8192바이트입니다.
// 서버에서 메시지 버퍼를 MAX_MESSAGE_BUFFER_SIZE로 확장했으므로 클라이언트도 이에 맞춰 읽을 수 있어야 합니다.
// 하지만 클라이언트의 BUFSIZ는 그대로 두고, 서버의 MAX_MESSAGE_BUFFER_SIZE가 BUFSIZ보다 크므로,
// 클라이언트는 BUFSIZ만큼만 읽을 수 있습니다.
// 여기서는 클라이언트도 서버와 동일하게 MAX_MESSAGE_BUFFER_SIZE를 사용하도록 변경합니다.
#define MAX_MESSAGE_BUFFER_SIZE (BUFSIZ + 256) // 서버와 동일한 크기로 맞춤

typedef struct {
    int type;
    char msg[BUFSIZ-4]; // 이 구조체는 현재 코드에서 사용되지 않으므로, 그대로 둡니다.
} data_t;

static int g_pfd[2], g_sockfd, g_cont = 1;

// C99, C11에 대응하기 위해서 사용
inline void clrscr(void);
void clrscr(void)
{
    write(1, "\033[1;1H\033[2J", 10); // ANSI escape 코드로 화면 지우기
}

void sigHandler(int signo)
{
    if(signo == SIGUSR1) {
        char buf[MAX_MESSAGE_BUFFER_SIZE]; // 서버의 MAX_MESSAGE_BUFFER_SIZE에 맞춰 버퍼 크기 조정
        memset(buf, 0, MAX_MESSAGE_BUFFER_SIZE); // 버퍼 초기화

        // FIX: 서버는 g_sockfd(네트워크 소켓)를 통해 메시지를 보냅니다.
        // 따라서 g_pfd[0] (클라이언트 내부 파이프)가 아닌 g_sockfd에서 읽어야 합니다.
        int n = read(g_sockfd, buf, MAX_MESSAGE_BUFFER_SIZE - 1); // -1은 널 종료 공간 확보

        if (n > 0) {
            buf[n] = '\0'; // 널 종료
            // FIX: 메시지를 서버로 다시 보내는 대신, 표준 출력(화면)에 출력합니다.
            write(STDOUT_FILENO, buf, strlen(buf)); // n 대신 strlen(buf) 사용
            fflush(stdout); // 즉시 화면에 출력되도록 버퍼 비움
        } else if (n == 0) {
            // 서버가 연결을 끊었습니다.
            g_cont = 0;
            printf("Server disconnected.\n");
        } else {
            // 읽기 오류 처리
            if (errno == EINTR) {
                // 시그널에 의해 read가 중단된 경우, 다시 시도할 필요는 없으나 오류로 간주하지 않습니다.
            } else {
                perror("read from socket in sigHandler");
            }
        }
    } else if(signo == SIGCHLD) {
        // 자식 프로세스 (stdin 리더)가 종료되었을 때 부모가 이 시그널을 받습니다.
        // g_cont를 0으로 설정하여 클라이언트 전체를 종료합니다.
        // 자식이 종료되면 보통 연결이 끊어진 것으로 간주합니다.
        g_cont = 0;
        printf("Connection is lost\n");
    }
}

int main(int argc, char** argv)
{
    struct sockaddr_in servaddr;
    int pid;
    char buf[MAX_MESSAGE_BUFFER_SIZE]; // 버퍼 크기 조정

    clrscr(); // 화면 지우기

    if(argc < 3) {
        fprintf(stderr, "usage : %s IP_ADDR PORT_NO\\n", argv[0]);
        return -1;
    }

    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(g_sockfd < 0) {
        perror("socket");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(atoi(argv[2]));

    // 서버에 연결 시도
    if (connect(g_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        close(g_sockfd);
        return -1;
    }
    printf("Connected to server.\n");

    // 클라이언트 프로세스 내에서 파이프 생성
    // 이 파이프는 자식 프로세스(STDIN 읽기)와 부모 프로세스(서버 응답 읽기) 간의 통신용이 아닙니다.
    // 이는 자식 프로세스가 표준 입력을 읽어서 부모 프로세스(서버에 메시지 보내는 역할)로 전달하는 데 사용됩니다.
    if (pipe(g_pfd) == -1) {
        perror("pipe");
        close(g_sockfd);
        return -1;
    }

    // 시그널 핸들러 설정
    // 부모 프로세스에서만 SIGCHLD를 처리해야 합니다.
    // SIGUSR1은 서버가 이 클라이언트에게 메시지가 도착했음을 알릴 때 사용됩니다.
    signal(SIGUSR1, sigHandler);
    signal(SIGCHLD, sigHandler); // 부모 프로세스가 자식의 종료를 처리

    if((pid = fork()) < 0) {
        perror("fork()");
        close(g_sockfd);
        close(g_pfd[0]); close(g_pfd[1]);
        return -1;
    } else if (pid == 0) { // 자식 프로세스 (표준 입력 읽기 및 서버로 전송)
        // 자식은 서버 소켓 g_sockfd를 부모와 공유합니다.
        // 자식은 부모에게 메시지를 전달할 g_pfd[1] (쓰기)를 사용합니다.
        close(g_pfd[0]); // g_pfd의 읽기 끝은 사용하지 않음

        // 자식 프로세스는 SIGUSR1/SIGCHLD 핸들러를 별도로 설정하지 않고, 부모로부터 상속받거나 기본 동작으로 둡니다.
        // 단, 시그널 핸들러에서 부모-자식 관계의 동작을 주의해야 합니다.
        // 여기서는 부모의 SIGCHLD 핸들러가 자식의 종료를 처리하도록 둡니다.
        // 자식은 자신의 SIGCHLD를 처리할 필요가 없습니다.
        signal(SIGCHLD, SIG_DFL); // 자식은 자신의 SIGCHLD를 처리하지 않음
        signal(SIGUSR1, SIG_DFL); // 자식은 서버로부터의 SIGUSR1을 직접 처리하지 않음 (부모가 처리)

        do {
            memset(buf, 0, MAX_MESSAGE_BUFFER_SIZE);
            // 사용자 입력을 읽어 서버로 보냅니다.
            // fgets는 줄바꿈 문자를 포함하여 읽습니다.
            if (fgets(buf, MAX_MESSAGE_BUFFER_SIZE, stdin) == NULL) {
                // EOF (Ctrl+D) 또는 읽기 오류 발생 시 종료
                g_cont = 0; // 부모에게 종료를 알리기 위해 파이프에 메시지를 보낼 수도 있지만, 여기서는 그냥 종료.
                break;
            }
            write(g_sockfd, buf, strlen(buf)); // 서버 소켓으로 직접 전송
        } while (g_cont);

        close(g_sockfd); // 자식은 서버 소켓을 닫습니다.
        close(g_pfd[1]); // g_pfd의 쓰기 끝을 닫습니다.
        exit(0); // 자식 프로세스 종료
    } else { // 부모 프로세스 (서버로부터 메시지 받기)
        // 부모는 표준 입력에서 읽는 g_pfd[1] (쓰기)를 사용하지 않습니다.
        close(g_pfd[1]); // g_pfd의 쓰기 끝을 닫음

        // 부모는 서버로부터의 메시지를 SIGUSR1 시그널 핸들러를 통해 비동기적으로 처리합니다.
        // 이 메인 루프에서는 특별히 할 일 없이 g_cont가 0이 될 때까지 대기합니다.
        // 실제로는 select() 또는 poll()을 사용하여 g_sockfd와 g_pfd[0] (자식의 종료 또는 메시지)를 동시에 감시하는 것이 일반적입니다.
        // 하지만 주어진 코드 구조에서는 시그널 핸들러가 메시지 수신을 담당합니다.
        while(g_cont) {
            // 이 루프는 g_cont가 0이 되거나 시그널이 올 때까지 대기합니다.
            // CPU 낭비를 줄이기 위해 짧은 sleep을 넣거나, 시그널을 기다리는 함수를 사용할 수 있습니다.
            // pause(); // 시그널이 올 때까지 프로세스를 일시 중지
            sleep(1); // 1초 대기 (CPU 낭비 방지)
        }

        // 자식 프로세스 종료를 기다립니다.
        int status;
        waitpid(pid, &status, 0); // 자식 종료 기다림

        close(g_sockfd); // 부모는 서버 소켓을 닫습니다.
        close(g_pfd[0]); // g_pfd의 읽기 끝을 닫습니다.
    }

    printf("Client shutting down.\n");
    return 0;
}
