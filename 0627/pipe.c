#include <stdio.h>
#include <unistd.h>

int main() {
    int fd[2];
    char msg[] = "Hello from parent!";
    char buf[100];

    pipe(fd);

    if (fork() == 0) {
        close(fd[1]); // 자식은 읽기만
        read(fd[0], buf, sizeof(buf));
        printf("자식이 받은 메시지: %s\n", buf);
    } else {
        close(fd[0]); // 부모는 쓰기만
        write(fd[1], msg, sizeof(msg));
    }

    return 0;
}

