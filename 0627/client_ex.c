#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_RESET   "\x1b[0m"

static int g_pfd[2];
static int g_sockfd;
static volatile sig_atomic_t g_cont = 1;

inline void clrscr(void){
	write(1, "",10);
}


