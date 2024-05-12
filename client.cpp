// #include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace std;

#define DEFAULT_BUF_SIZE 512
#define DEFAULT_NICK_SIZE 32
#define DEFAULT_BYTES_SIZE 4

ssize_t tcp_recv(int sockfd, char *buf, size_t len) {
    for (size_t index = 0; index < len; ) {
        int result = recv(sockfd, buf + index, len - index, 0);
        if (result <= 0) {
            return -1;
        }
        index += result;
    }
    return 0;
}

ssize_t tcp_send(int sockfd, char *buf, size_t len) {
    for (size_t index = 0; index < len; ) {
        int result = send(sockfd, buf + index, len - index, 0);
        if (result <= 0) {
            return -1;
        }
        index += result;
    }
    return 0;
}


int serverSocket = 0;  // 服务器套接字
char* clientNickname;  // 客户端昵称
pthread_mutex_t messageMutex = PTHREAD_MUTEX_INITIALIZER;

/* 从字符串中移除换行符 */
void removeNewline(char* arr, int length) {
  int i = 0;
  while (i < length && arr[i] != '\n') {
      i++;
  }

  if (i < length) {
      arr[i] = '\0';
}}

/*
  按照协议标准发送消息：
  先发送消息大小，然后发送消息内容
*/
int sendMessage(char* message) {
    uint32_t messageSize = htonl(strlen(message));
    int result = tcp_send(serverSocket, (char*)&messageSize, DEFAULT_BYTES_SIZE);
    if (result < 0) return -1;
    result = tcp_send(serverSocket, message, strlen(message));
    if (result < 0) return -1;

    return 0;
}

void* sendMessages(void* args) {
    if (args != NULL) exit(EXIT_FAILURE);
    char message[DEFAULT_BUF_SIZE] = {};
    memset(message, 0, DEFAULT_BUF_SIZE);

    char input[5];

    bool continueLoop = true;
    while (continueLoop) {
        printf("输入'm'来输入消息:\n");

        // 读取除换行符外的所有字符，不超过缓冲区大小-1，并将其存储
        if (scanf(" %4[^\n]%*c", input) < 0) {
            perror("scanf() 出错");
            exit(EXIT_FAILURE);
        }
        fflush(stdin);

        if (strcmp(input, "m\0") == 0) {
            pthread_mutex_lock(&messageMutex);
            printf("输入消息:\n");
            if (!fgets(message, DEFAULT_BUF_SIZE, stdin)) {
                perror("fgets() 出错");
                exit(EXIT_FAILURE);
            }

            pthread_mutex_unlock(&messageMutex);
            removeNewline(message, sizeof(message));

            if (strlen(message) == 0) {
                printf("没有可发送的消息。\n");
            } else {
                int sendNicknameResult = sendMessage(clientNickname);
                int sendMessageResult = sendMessage(message);

                if (sendNicknameResult < 0 || sendMessageResult < 0) {
                    perror("套接字写入错误");
                }
            }
        } else {
            continueLoop = false;
        }
        memset(message, 0, DEFAULT_BUF_SIZE);
    }
    pthread_exit(NULL);
}

/* 接收服务器消息并输出 */
void* receiveMessages(void* args) {
    if (args != NULL) exit(EXIT_FAILURE);

    int leaveFlag = 0;
    uint32_t nicknameSize = 0;
    uint32_t messageSize = 0;
    uint32_t dateSize = 0;
    char* nickBuffer = NULL;
    char* messageBuffer = NULL;
    char* dateBuffer = NULL;

    while (true) {
        if (leaveFlag) break;

        // 获取昵称大小
        int receive = tcp_recv(serverSocket, (char*)&nicknameSize, DEFAULT_BYTES_SIZE);

        if (receive == 0 && nicknameSize > 0) {
            // 如果没有错误，获取昵称
            nicknameSize = ntohl(nicknameSize);

            nickBuffer = (char*)malloc(sizeof(char) * (nicknameSize + 1));
            memset(nickBuffer, 0, (nicknameSize + 1));
            receive = tcp_recv(serverSocket, nickBuffer, nicknameSize);

            if (receive == 0) {
                // 如果没有错误，获取消息大小
                receive = tcp_recv(serverSocket, (char*)&messageSize, DEFAULT_BYTES_SIZE);

                if (receive == 0 && messageSize > 0) {
                    // 如果没有错误，获取消息内容
                    messageSize = ntohl(messageSize);

                    messageBuffer = (char*)malloc(sizeof(char) * (messageSize + 1));
                    memset(messageBuffer, 0, (messageSize + 1));
                    receive = tcp_recv(serverSocket, messageBuffer, messageSize);

                    if (receive == 0) {
                        // 如果没有错误，获取日期大小
                        receive = tcp_recv(serverSocket, (char*)&dateSize, DEFAULT_BYTES_SIZE);

                        if (receive == 0 && dateSize > 0) {
                            // 如果没有错误，获取日期
                            dateSize = ntohl(dateSize);
                            dateBuffer = (char*)malloc(sizeof(char) * (dateSize + 1));
                            memset(dateBuffer, 0, (dateSize + 1));
                            receive = tcp_recv(serverSocket, dateBuffer, dateSize);

                            if (receive == 0) {
                                pthread_mutex_lock(&messageMutex);
                                printf("{%s} [%s] %s\n", dateBuffer, nickBuffer, messageBuffer);
                                pthread_mutex_unlock(&messageMutex);
                            }
                            free(dateBuffer);
                        }
                    }
                    free(messageBuffer);
                }
            }
            free(nickBuffer);
        } 
        else {
            printf("错误: -1\n");
            leaveFlag = 1;
        }
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    uint16_t portNumber;
    char hostname[50];
    struct sockaddr_in serverAddress;
    struct hostent* server;

    if (argc < 3) {
        fprintf(stderr,"请输入服务器主机名、端口和您的昵称 %s\n",argv[0]);
        exit(EXIT_SUCCESS);
    }

    strncpy(hostname, argv[1], sizeof(hostname) - 1);
    portNumber = (uint16_t)atoi(argv[2]);
    clientNickname = argv[3];

    /* 创建套接字 */
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0) {
        perror("打开套接字出错");
        exit(EXIT_FAILURE);
    }

    /* 获取主机名对应的服务器结构 hostent */
    server = gethostbyname(hostname);

    if (server == NULL) {
        fprintf(stderr, "错误，找不到主机\n");
        exit(EXIT_FAILURE);
    }

    /* 设置服务器 sockaddr_in 结构 */
    bzero((char*)&serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    bcopy(server->h_addr, (char*)&serverAddress.sin_addr.s_addr, (size_t)server->h_length);
    serverAddress.sin_port = htons(portNumber);

    /* 连接服务器 */
    if (connect(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        perror("连接出错");
        exit(EXIT_FAILURE);
    }

    puts("连接成功");
    puts(">>>> 欢迎来到聊天室 <<<<");

    pthread_t sendMessagesThread;
    int result = pthread_create(&sendMessagesThread, NULL, sendMessages, NULL);
    if (result != 0) {
        fprintf(stderr, "错误: pthread 创建失败，错误码：%d\n", result);
        return EXIT_FAILURE;
    }

    pthread_t receiveMessagesThread;
    result = pthread_create(&receiveMessagesThread, NULL, receiveMessages, NULL);
    if (result != 0) {
        fprintf(stderr, "错误: pthread 创建失败，错误码：%d\n", result);
        return EXIT_FAILURE;
    }

    pthread_join(sendMessagesThread, NULL);
    pthread_join(receiveMessagesThread, NULL);

    pthread_exit(NULL);
    close(serverSocket);

    return 0;
}
