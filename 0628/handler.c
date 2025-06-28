#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

void handler_signal(int sog) {
	printf("자식 프로세스: 시그널 수신 완료! (SIGUSR1)\n");
	exit(0);
}

int main() {
	pid_t pid = fork();

	if (pid < 0) {
		perror("fork 실패");
		exit(1);
	}
	else if (pid == 0) {
		signal(SIGUSR1, handler_signal);
		printf("자식 프로세스 대기 중... (PID=%d)\n", getpid());
		while (1) pause();
	}
	else {
		sleep(2);
		printf("부모 프로세스: 자식에게 SIGUSR1 전송 (PID=%d)\n", pid);
		kill(pid, SIGUSR1);
		wait(NULL);
		printf("부모 프로세스: 자식 종료 확인\n");
	}

	return 0;
}
