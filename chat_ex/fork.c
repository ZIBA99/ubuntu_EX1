#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

int main(){
	pid_t pid =fork();

	if (pid == -1){
		perror ("fork error");
		return 1;
	}else if (pid == 0) {
		printf("child process : PID = %d\n", getpid());
	}else {
		printf("parent process : PID = %d, child PID = %d\n", getpid(), pid);
	}

	return 0;
}
