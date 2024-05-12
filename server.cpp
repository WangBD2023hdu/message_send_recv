#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>

#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>

using namespace std;

#define DEFAULT_BUF_SIZE 512
#define DEFAULT_NICK_SIZE 32
#define DEFAULT_BYTES_SIZE 4
#define MAX_CLIENTS 10

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

struct client_t {
  int id;
  int serverSocket;
  char clientNickname[DEFAULT_NICK_SIZE];
};

static unsigned int clients_num = 0;
client_t *clients[MAX_CLIENTS];  // ID从1开始
pthread_mutex_t messageMutex = PTHREAD_MUTEX_INITIALIZER;

/* 将客户端添加到队列中 */
void add_client(client_t *cli) {
  pthread_mutex_lock(&messageMutex);

  int i = 0;
  while (i < MAX_CLIENTS) {
    if (!clients[i]) {
      clients[i] = cli;
      clients[i]->id = i + 1;
      break;
    }
    i++;
  }

  pthread_mutex_unlock(&messageMutex);
}

/* 从队列中移除客户端 */
void remove_client(int id) {
  pthread_mutex_lock(&messageMutex);

  int i = 0;
  while (i < MAX_CLIENTS) {
    if (clients[i]) {
      if (clients[i]->id == id) {
        clients[i] = NULL;
        break;
      }
    }
    i++;
  }

  pthread_mutex_unlock(&messageMutex);
}

/*
根据协议标准发送消息:
消息大小，然后是消息正文
*/
int send_msg(int serverSocket, char *msg) {
  uint32_t msg_size = htonl(strlen(msg));
  int res = tcp_send(serverSocket, (char *)&msg_size, DEFAULT_BYTES_SIZE);
  if (res == 0) {
    res = tcp_send(serverSocket, msg, strlen(msg));
    if (res < 0) return -1;
  } else
    return -1;

  return 0;
}

/* 向所有客户端发送消息 */
void broadcast_message(char *s) {
  pthread_mutex_lock(&messageMutex);

  int i = 0;
  while (i < MAX_CLIENTS) {
    if (clients[i]) {
      if (send_msg(clients[i]->serverSocket, s) < 0) {
        perror("错误:发送描述符失败");
        continue;
      }
    }
    i++;
  }

  pthread_mutex_unlock(&messageMutex);
}

void *handle_client(void *arg) {
  char buffer[DEFAULT_BUF_SIZE];
  int leave_flag = 0;
  uint32_t nickname_size = 0;
  uint32_t msg_size = 0;

  clients_num++;
  client_t *cli = (client_t *)arg;

  while (true) {
    if (leave_flag) {
      break;
    }
    // 获取nick size
    int receive =tcp_recv(cli->serverSocket, (char *)&nickname_size, DEFAULT_BYTES_SIZE);

    if (receive == 0 && nickname_size > 0) {
      // 如果没有错误，获取nickname
      nickname_size = ntohl(nickname_size);

      char *nick_buf = (char *)malloc(sizeof(char) * (nickname_size + 1));
      memset(nick_buf, 0, (nickname_size + 1));
      receive = tcp_recv(cli->serverSocket, nick_buf, nickname_size);

      if (receive == 0) {
        // 如果没有错误，将昵称分配给client对象
        strncpy(cli->clientNickname, nick_buf, nickname_size + 1);
        // 获取消息大小
        receive = tcp_recv(cli->serverSocket, (char *)&msg_size, DEFAULT_BYTES_SIZE);

        if (receive == 0 && msg_size > 0) {
          // 如果没有错误，获取消息
          msg_size = ntohl(msg_size);

          char *msg_buf = (char *)malloc(sizeof(char) * (msg_size + 1));
          memset(msg_buf, 0, (msg_size + 1));
          receive = tcp_recv(cli->serverSocket, msg_buf, msg_size);

          if (receive == 0) {
            time_t t = time(NULL);
            struct tm *lt = localtime(&t);
            sprintf(buffer, "%02d:%02d", lt->tm_hour, lt->tm_min);

            broadcast_message(nick_buf);
            broadcast_message(msg_buf);
            broadcast_message(buffer);
          }
          free(msg_buf);
        }
      }
      free(nick_buf);
    } else if (receive == 0) {
      sprintf(buffer, "Client with id %d has left\n", cli->id);
      printf("%s", buffer);
      leave_flag = 1;
    } else {
      printf("ERROR: -1\n");
      sprintf(buffer, "Client with id %d has left\n", cli->id);
      printf("%s", buffer);
      leave_flag = 1;
    }
    memset(buffer, 0, DEFAULT_BUF_SIZE);
  }

  /* 从队列中删除客户端并退出线程 */
  close(cli->serverSocket);
  remove_client(cli->id);
  free(cli);
  clients_num--;
  pthread_detach(pthread_self());

  return NULL;
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  int server_sockfd, newsockfd;
  uint16_t portno;
  struct sockaddr_in serv_addr, cli_addr;
  pthread_t tid;

  // 检查参数
  if (argc < 2) {
    fprintf(stderr, "您忘记输入端口号 %s.\n", argv[0]);
    exit(EXIT_SUCCESS);
  }

  portno = (uint16_t)atoi(argv[1]);

  // 初始化套接字
  server_sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (server_sockfd < 0) {
    perror("打开服务器套接字时出错");
    exit(EXIT_FAILURE);
  }

  // 初始化套接字结构体
  bzero((char *)&serv_addr, sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;  // inet_addr(ip.c_str());
  serv_addr.sin_port = htons(portno);

  // 绑定
  bind(server_sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

  // 一次最多监听5个请求
  printf("监听中......\n");
  listen(server_sockfd, 5);

  printf("------ 欢迎来到聊天室 ------\n");

  // Accept actual connections from the clients
  while (true) {
    unsigned int clilen = sizeof(cli_addr);
    newsockfd = accept(server_sockfd, (struct sockaddr *)&cli_addr, &clilen);

    if (newsockfd < 0) {
      perror("接受客户端套接字时出错");
      continue;
    }

    if (clients_num < MAX_CLIENTS) {
      // Add a client
      client_t *cli = (client_t *)malloc(sizeof(client_t));
      cli->serverSocket = newsockfd;
      add_client(cli);
      printf("客户端：id %d 已加入聊天\n", cli->id);

      // Create a thread process for that client
      pthread_create(&tid, NULL, &handle_client, (void *)cli);
      pthread_detach(tid);
    } else {
      printf("服务器已满\n");
      close(newsockfd);
      continue;
    }
  }

  // Close listening socket
  close(server_sockfd);
  pthread_exit(NULL);

  printf("已经成功结束\n");

  return 0;
}
