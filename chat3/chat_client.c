#include <stdio.h>      // 표준 입출력 함수 (printf, fgets 등)
#include <string.h>     // 문자열 관련 함수 (memset, strlen, strcmp 등)
#include <unistd.h>     // POSIX 운영체제 API (write, close, fork, pipe, getppid 등)
#include <stdlib.h>     // 일반 유틸리티 함수 (atoi, exit 등)
#include <signal.h>     // 시그널 처리 함수 (signal, kill)
#include <sys/wait.h>   // 자식 프로세스 대기 함수 (wait)
#include <sys/ioctl.h>  // I/O 제어 함수 (현재 코드에서 직접 사용되지는 않음)
#include <sys/socket.h> // 소켓 함수 (socket, connect)
#include <arpa/inet.h>  // 인터넷 주소 변환 함수 (inet_pton, htons)

// 터미널 색상 코드를 위한 매크로 정의
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m" // 색상 초기화

// 데이터 전송을 위한 구조체 (현재 코드에서 type 필드는 사용되지 않음)
typedef struct {
	int type;             // 데이터의 종류 (예: 일반 메시지, 접속/퇴장 알림 등)
	char msg[BUFSIZ-4];   // 메시지 내용 (BUFSIZ는 <stdio.h>에 정의된 버퍼 크기)
} data_t;

// 전역 변수 선언
static int g_pfd[2];      // 파이프 파일 디스크립터 (g_pfd[0]: 읽기, g_pfd[1]: 쓰기)
static int g_sockfd;      // 소켓 파일 디스크립터
static int g_cont = 1;    // 프로그램 계속 실행 여부를 제어하는 플래그 (1: 계속, 0: 종료)

// 화면을 지우는 함수
// inline 키워드는 컴파일러에게 함수를 인라인으로 처리하도록 권장 (C99, C11 표준)
inline void clrscr(void);
void clrscr(void)
{
    // ANSI escape 코드를 사용하여 터미널 화면 지우기 (커서를 (1,1)로 이동 후 화면 지우기)
    write(1, "\033[1;1H\033[2J", 10);
}

// 시그널 핸들러 함수
void sigHandler(int signo)
{
	if(signo == SIGUSR1) { // 사용자 정의 시그널 1 (SIGUSR1)이 발생했을 때
		char buf[BUFSIZ];
		// 파이프의 읽기 end (g_pfd[0])에서 데이터를 읽어옴
		int n = read(g_pfd[0], buf, BUFSIZ);
		// 읽어온 데이터를 소켓 (g_sockfd)을 통해 서버로 전송
		write(g_sockfd, buf, n);
	} else if(signo == SIGCHLD) { // 자식 프로세스가 종료되었을 때 (SIGCHLD)
		g_cont = 0; // 프로그램 종료 플래그를 0으로 설정
		printf("Connection is lost\n"); // 연결 끊김 메시지 출력
	}
}

// 메인 함수
int main(int argc, char** argv)
{
	struct sockaddr_in servaddr; // 서버 주소 정보를 저장할 구조체
	int pid;                     // fork() 함수의 반환값 (프로세스 ID)
	char buf[BUFSIZ];            // 메시지 송수신을 위한 버퍼

	clrscr(); // 화면 지우기

	// 명령줄 인자 개수 확인 (IP 주소와 포트 번호 필요)
	if(argc < 3) {
		fprintf(stderr, "usage : %s IP_ADDR PORT_NO\n", argv[0]);
		return -1; // 에러 코드 반환
	}

	// TCP 소켓 생성 (AF_INET: IPv4, SOCK_STREAM: TCP, 0: 기본 프로토콜)
	g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(g_sockfd < 0) {
		perror("socket"); // 소켓 생성 실패 시 에러 메시지 출력
		return -1;
	}

	// servaddr 구조체 초기화
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET; // 주소 체계: IPv4
	// 문자열 IP 주소(argv[1])를 네트워크 주소로 변환하여 저장
	inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
	// 문자열 포트 번호(argv[2])를 정수로 변환 후 네트워크 바이트 순서로 변환하여 저장
	servaddr.sin_port = htons(atoi(argv[2]));

	// 서버에 연결 시도
	if (connect(g_sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect"); // 연결 실패 시 에러 메시지 출력
        close(g_sockfd);
        return -1;
    }

	// 파이프 생성 (부모-자식 프로세스 간 통신용)
	pipe(g_pfd);

	// 프로세스 포크 (부모-자식 분리)
	if((pid = fork()) < 0) {
		perror("fork( )"); // fork 실패 시 에러 메시지 출력
        close(g_sockfd);
        close(g_pfd[0]);
        close(g_pfd[1]);
		return -1;
	} else if (pid == 0) { // 자식 프로세스 (메시지 입력 및 파이프 쓰기 담당)
		// SIGCHLD 시그널 핸들러 설정 (부모가 종료될 때 자식도 종료되도록)
		signal(SIGCHLD, sigHandler);
		close(g_pfd[0]); // 자식은 파이프의 읽기 end를 닫음 (쓰기만 사용)
		do {
			memset(buf, 0, BUFSIZ); // 버퍼 초기화
			printf(COLOR_BLUE "\r> " COLOR_RESET); // 사용자 입력 프롬프트 출력 (파란색)
			fflush(NULL); // 출력 버퍼 비우기 (프롬프트가 즉시 보이도록)
			fgets(buf, BUFSIZ, stdin); // 표준 입력(키보드)에서 문자열 읽기
			// 입력받은 데이터를 파이프의 쓰기 end (g_pfd[1])에 씀
			write(g_pfd[1], buf, strlen(buf)+1); // +1은 널 종료 문자 포함
			// 부모 프로세스에게 SIGUSR1 시그널을 보내어 새로운 입력이 있음을 알림
			kill(getppid(), SIGUSR1);
		} while (strcmp(buf, "quit\n") != 0 && g_cont); // "quit" 입력 또는 g_cont가 0이 될 때까지 반복
		// "quit\n"으로 비교하는 이유는 fgets가 개행 문자도 포함하기 때문

		close(g_pfd[1]); // 자식 프로세스 종료 시 파이프의 쓰기 end 닫음
	} else { // 부모 프로세스 (서버로부터 메시지 수신 및 화면 출력 담당)
		// SIGUSR1 시그널 핸들러 설정 (자식이 보낸 메시지 처리)
		signal(SIGUSR1, sigHandler);
		// SIGCHLD 시그널 핸들러 설정 (자식이 종료될 때 부모도 종료되도록)
		signal(SIGCHLD, sigHandler);
		close(g_pfd[1]); // 부모는 파이프의 쓰기 end를 닫음 (읽기만 사용)
		while(g_cont) { // g_cont가 1인 동안 계속 실행
			memset(buf, 0, BUFSIZ); // 버퍼 초기화
			// 소켓 (g_sockfd)에서 서버로부터 데이터 읽기
			int n = read(g_sockfd, buf, BUFSIZ);
			if(n <= 0) break; // 읽은 데이터가 없거나 에러 발생 시 루프 종료 (연결 끊김)
			printf(COLOR_GREEN "\r%s" COLOR_RESET, buf); // 수신된 메시지를 초록색으로 출력
			printf(COLOR_BLUE "\r> " COLOR_RESET); // 사용자 입력 프롬프트 다시 출력
			fflush(NULL); // 출력 버퍼 비우기
		}
		close(g_pfd[0]); // 부모 프로세스 종료 시 파이프의 읽기 end 닫음
		kill(pid, SIGCHLD); // 자식 프로세스에게 SIGCHLD 시그널을 보내 종료를 알림
		wait(NULL); // 자식 프로세스가 완전히 종료될 때까지 기다림
	}

	close(g_sockfd); // 소켓 닫기

	return 0; // 프로그램 정상 종료
}
