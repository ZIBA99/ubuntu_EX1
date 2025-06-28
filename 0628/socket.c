#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h> //??


int main() {
	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {AF_INET,htons(12345), INADDR_ANY};

	bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
	listen(server_fd, 1);

	int client_fd = accept(server_fd, NULL, NULL);
	write(client_fd, "Hello Client!", 14);
	close(client_fd);
	close(server_fd);
	return 0;
}
