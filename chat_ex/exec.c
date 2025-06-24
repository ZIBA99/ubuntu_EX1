#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
	printf("before esec\n");

	execl("/bin/ls", "ls", "-l", NULL);
	perror("exec error");

	return 0;
}	
