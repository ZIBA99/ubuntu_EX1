#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main() {
	int fd[2];
	pipe(fd);

	if (fork() == 0){
		close(fd[1]);//write close
		char msg[100];
		read(fd[0], msg, sizeof(msg));
		printf("자식이 읽음: %s\n", msg);
	} else {
		close(fd[0]);
		char *msg = "Hello from parent!";
		write(fd[1], msg, strlen(msg) + 1);
	}
	return 0;
}
