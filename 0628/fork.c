#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

int main() {
	pid_t pid = fork();

	if(pid < 0) {
		perror("fork 실패");
		exit(1);
	}
	else if (pid == 0) {
        // 자식 프로세스
        printf("자식 프로세스: PID=%d, 부모 PID=%d\n", getpid(), getppid());
        sleep(2); // 실행 순서 확인용
        printf("자식 프로세스 종료\n");
        exit(0);
	}
	else {
		printf("부모 프로세스: 자식 PID=%d\n", pid);
		int status;
		wait(&status);
		printf("부모 프로세스: 자식 종료 확인, 이제 나도 종료\n");
	}
	
	return 0;
}
