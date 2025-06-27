#include <stdio.h>
#include <unistd.h>
#include <signal.h>

void handler(int signo) {
	printf("SIGUSR1 신호 수신!\n");
}

int main(){
	signal(SIGUSR1, handler);

	if (fork() == 0){
		sleep(1);
		kill(getppid(), SIGUSR1);

	}else{
		pause();
	}

	return 0;
}
