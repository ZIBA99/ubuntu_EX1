#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

int main()
{
	pid_t pid = fork();

	if (pid == 0){
		printf("child: Doing something...\n");
	        sleep(2);
		printf("child: Done.\n");
		return 42;
	} else {
		int status;
		wait(&status);
		printf("parent: child exited with status %d\n", WEXITSTATUS(status));
	}

	return 0;
}	
