#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

int main() {
	int fd[2];
	pid_t pid;

	if (pipe(fd) == -1){
		perror("pipe");
		exit(1);
	}

	pid = fork();

	if (pid < 0){
		perror("fork");
		exit(1);
	}
	else if (pid == 0){
		close(fd[1]);

		int nums[2];
		read(fd[0], nums, sizeof(nums));

		int sum = nums[0] + nums[1];
		printf("자식 프로세스: 받은 값 %d + %d = %d\n", nums[0], nums[1], sum);

		close(fd[0]);
	}

	else{
		close(fd[0]);

		int nums[2] = {3,7};
		write(fd[1], nums, sizeof(nums));

		close(fd[1]);
		wait(NULL);
	}

	return 0;
}
