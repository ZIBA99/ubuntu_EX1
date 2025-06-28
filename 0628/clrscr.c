#include <stdio.h>
#include <string.h>
#include <unistd.h>

void clrscr(void)              
{
    write(1, "\033[1;1H\033[2J", 10);
}

int main() {
    // 화면에 무언가 출력
    char qwe[256];
    sprintf(qwe, "이 텍스트는 곧 지워집니다...\n");
    write(1, qwe, strlen(qwe));
    //write(1, "이 텍스트는 곧 지워집니다...\n", strlen(qwe));
    
    sleep(2);  // 2초 대기
    
    // 화면 지우기
    clrscr();
    
    // 새로운 내용 출력
    printf(qwe, "화면이 지워지고 새로운 내용이 표시됩니다.");
    write(1, qwe, strlen(qwe));
    //write(1, "화면이 지워지고 새로운 내용이 표시됩니다.\n", str;len(qwe));
    
    return 0;
}
