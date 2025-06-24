#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

void handler(int sig){
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0){
		printf("Handler: child %d terminated\n", pid);
	}
}

int main(){
	signal(SIGCHLD, handler);

	for (int i = 0; i < 3; i++){
		if (fork() == 0 ) {
			printf("child %d started\n", getpid());
			sleep(1 + i);
			exit(0);
		}
	}

	while (1){
		pause();
	}

	return 0;
}
