//chat_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// 터미널 UI를 위한 ANSI 이스케이프 코드
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

// 전역 변수: 시그널 핸들러와 main 함수에서 공유해야 함
static int g_sockfd;      // 서버와 통신을 위한 소켓 파일 디스크립터
static int g_pipe_fd[2];  // 부모-자식 프로세스 간 통신을 위한 파이프
static int g_continue = 1; // 프로그램의 주 루프를 제어하는 플래그

// 화면을 지우는 함수
void clear_screen(void) {
    // ANSI 이스케이프 코드를 사용하여 화면을 지우고 커서를 (1,1)로 이동
    write(STDOUT_FILENO, "\033[1;1H\033[2J", 10);
}

// 시그널 핸들러
void sig_handler(int signo) {
    if (signo == SIGUSR1) {
        // 자식 프로세스(키보드 입력 담당)로부터 신호를 받았을 때
        char buf[BUFSIZ];
        memset(buf, 0, BUFSIZ);

        // 파이프로부터 데이터를 읽어 서버로 전송
        int n = read(g_pipe_fd[0], buf, BUFSIZ);
        if (n > 0) {
            write(g_sockfd, buf, n);
        }
    } else if (signo == SIGCHLD) {
        // 자식 프로세스가 종료되었을 때 (서버 연결이 끊긴 후 부모가 자식을 종료시켰거나, 자식 스스로 종료)
        g_continue = 0; // 메인 루프를 종료하도록 플래그 설정
    } else if (signo == SIGINT || signo == SIGTERM) {
        // Ctrl+C (SIGINT) 또는 종료(SIGTERM) 시그널을 받았을 때
        printf("\n" COLOR_YELLOW "Chat client is shutting down." COLOR_RESET "\n");
        g_continue = 0; // 메인 루프 종료
    }
}

int main(int argc, char** argv) {
    struct sockaddr_in serv_addr;
    pid_t pid;
    char buf[BUFSIZ];

    if (argc < 3) {
        fprintf(stderr, "사용법: %s <서버 IP 주소> <포트 번호>\n", argv[0]);
        return -1;
    }

    clear_screen();
    printf(COLOR_YELLOW "Connecting to chat server...\n" COLOR_RESET);

    // 1. 소켓 생성
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) {
        perror("socket");
        return -1;
    }

    // 2. 서버 주소 설정 및 연결
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(g_sockfd);
        return -1;
    }
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(g_sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(g_sockfd);
        return -1;
    }
    
    printf(COLOR_GREEN "Successfully connected to the server.\n" COLOR_RESET);
    printf("Type your nickname and press Enter.\n");
    printf("To see commands, type " COLOR_CYAN "/help" COLOR_RESET " after setting a nickname.\n");


    // 3. IPC를 위한 파이프 생성
    if (pipe(g_pipe_fd) < 0) {
        perror("pipe");
        close(g_sockfd);
        return -1;
    }

    // 4. 프로세스 분기 (fork)
    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(g_sockfd);
        close(g_pipe_fd[0]);
        close(g_pipe_fd[1]);
        return -1;
    }

    if (pid > 0) {
        // ==================== 부모 프로세스: 서버로부터 메시지 수신 담당 ====================
        close(g_pipe_fd[1]); // 파이프의 쓰기용 끝을 닫음

        // 시그널 핸들러 설정
        signal(SIGUSR1, sig_handler); // 자식으로부터의 데이터 전송 요청
        signal(SIGCHLD, sig_handler); // 자식 프로세스 종료 감지
        signal(SIGINT, sig_handler);  // Ctrl+C 입력 감지
        signal(SIGTERM, sig_handler); // 종료 시그널 감지

        while (g_continue) {
            memset(buf, 0, BUFSIZ);
            int n = read(g_sockfd, buf, BUFSIZ - 1);

            if (n <= 0) { // 서버와 연결이 끊김
                if (n == 0) {
                    printf(COLOR_RED "\rServer has closed the connection.\n" COLOR_RESET);
                } else {
                    perror("\rread from server");
                }
                g_continue = 0; // 루프 종료
                break;
            }
            
            // 받은 메시지 출력 후, 사용자 입력을 위해 프롬프트를 다시 그려줌
            printf(COLOR_GREEN "\r%s" COLOR_RESET, buf);
            printf(COLOR_BLUE "\r> " COLOR_RESET);
            fflush(stdout); // 출력 버퍼를 즉시 비움
        }

        // 루프가 끝나면 자식 프로세스에게 종료 신호를 보냄
        if (pid > 0) {
            kill(pid, SIGTERM);
        }
        wait(NULL); // 자식 프로세스가 완전히 종료될 때까지 기다려 좀비 프로세스 방지
        close(g_pipe_fd[0]);

    } else {
        // ==================== 자식 프로세스: 사용자 키보드 입력 담당 ====================
        close(g_pipe_fd[0]); // 파이프의 읽기용 끝을 닫음
        
        // 자식 프로세스는 부모가 보낸 SIGTERM에 의해 종료되므로, 별도 핸들러는 불필요
        
        while (1) {
            printf(COLOR_BLUE "\r> " COLOR_RESET);
            fflush(stdout);

            memset(buf, 0, BUFSIZ);
            if (fgets(buf, BUFSIZ, stdin) == NULL) {
                // Ctrl+D (EOF) 입력 시 종료
                break;
            }
            
            // 파이프에 사용자 입력을 쓰고, 부모에게 SIGUSR1 신호를 보내 알림
            write(g_pipe_fd[1], buf, strlen(buf));
            kill(getppid(), SIGUSR1);

            // 사용자가 종료 명령어 입력 시 자식 프로세스 스스로 종료
            if (strncmp(buf, "/quit", 5) == 0) {
                break;
            }
        }
        close(g_pipe_fd[1]);
        exit(0); // 자식 프로세스 정상 종료
    }

    close(g_sockfd);
    printf(COLOR_YELLOW "Chat client terminated.\n" COLOR_RESET);
    return 0;
}
