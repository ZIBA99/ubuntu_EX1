#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "사용법: %s <원본파일> <복사파일>\n", argv[0]);
		exit(1);
	}

	int src_fd = open(argv[1], O_RDONLY);
	if (src_fd < 0) {
		perror("원본 파일 열기 실패");
		exit(1);
	}

	int dest_fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);// 0644 8진법,소유자,그룬,기타 (읽기4, 쓰기2) -> 8진법,소유자 읽쓰,그룹 일기,기타 읽기 권한
	if (dest_fd < 0) {
        perror("복사 파일 열기 실패");
        close(src_fd);
        exit(1);
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes;

    while ((bytes = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        write(dest_fd, buffer, bytes);
    }

    printf("복사가 완료되었습니다.\n");

    close(src_fd);
    close(dest_fd);

    return 0;
}
