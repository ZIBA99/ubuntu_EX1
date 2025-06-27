#include <stdio.h>
#include <unistd.h>

int main() {
    pid_t pid = fork();

    if (pid == 0) {
        printf("자식 프로세스입니다. PID: %d\n", getpid());
    } else {
        printf("부모 프로세스입니다. 자식 PID: %d\n", pid);
    }
    return 0;
}

