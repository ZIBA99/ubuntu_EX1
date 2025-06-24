#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

int main(int argc, char *argv[]){
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_addr, clnt_addr;
	socklen_t clnt_addr_size;

	char message[BUF_SIZE];

	if (argc != 2){
		printf("Usage: %s <port>\n", argv[0]);
		return 1;
	}

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (serv_sock == -1){
		perror("sockt error");
		exit(1);
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(argv[1]));

	if (bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1){
		perror("bind error");
		exit(1);
	}

	if(listen(serv_sock, 5) == -1) {
		perror("listen error");
		exit(1);
	}

	printf("서버가 연결을 기다리는 중...\n");

	clnt_addr_size = sizeof(clnt_addr);
	clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
	if (clnt_sock == -1){
		perror("accept error");
		exit(1);
	}

	while(1){
		int str_len = read(clnt_sock,message,BUF_SIZE);
		if (str_len == 0){
			printf("클라이언트 종료됨.\n");
			break;
		}

		write(clnt_sock, message, str_len);
	}

	close(clnt_sock);
	close(serv_sock);
	return 0;
}	
