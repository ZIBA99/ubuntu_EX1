// common.h (공통으로 사용될 상수, 구조체, 프로토콜 정의)
#ifndef COMMON_H
#define COMMON_H

#define MAX_BUFFER_SIZE 1024
#define PORT 8080 // 예시 포트 번호
#define MAX_NICKNAME_LEN 31 // 널 문자를 포함하여 32바이트
#define MAX_ROOM_NAME_LEN 31 // 널 문자를 포함하여 32바이트

// 메시지 타입 정의
enum MessageType {
    MSG_CHAT,           // 일반 채팅 메시지
    MSG_COMMAND,        // 서버 명령어 (예: /join, /list 등)
    MSG_WHISPER,        // 귓속말
    MSG_SYSTEM,         // 서버 시스템 메시지 (입장, 퇴장 등)
    MSG_NICKNAME_SET,   // 닉네임 설정 요청
    MSG_NICKNAME_ACK,   // 닉네임 설정 응답
    MSG_ERROR           // 오류 메시지
};

// 메시지 프로토콜 구조체
// C언어에서는 char 배열로 문자열을 다루고, 고정된 크기를 할당합니다.
typedef struct {
    int type; // 메시지 타입 (enum MessageType 참조)
    char sender_nickname[MAX_NICKNAME_LEN + 1]; // +1 for null terminator
    char target_nickname[MAX_NICKNAME_LEN + 1]; // 귓속말 대상 (+1 for null terminator)
    char room_name[MAX_ROOM_NAME_LEN + 1];      // 채팅방 이름 (+1 for null terminator)
    char message[MAX_BUFFER_SIZE];              // 실제 메시지 내용
} ChatMessage;

#endif // COMMON_H
