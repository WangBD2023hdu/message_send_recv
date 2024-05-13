#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>

#include <string.h>

#include <signal.h>
#define MAX_COUNT_CLIENTS 100

int clients[MAX_COUNT_CLIENTS];
char is_active[MAX_COUNT_CLIENTS];
int count_active_clients;

pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static inline int reserve_socket_cell() {
  pthread_mutex_lock(&mtx);
  count_active_clients++;
  if (count_active_clients > MAX_COUNT_CLIENTS) {
    count_active_clients--;
    return -1;
  }
  int i = 0;
  while (is_active[i] && i < MAX_COUNT_CLIENTS) {
    ++i;
  }
  is_active[i] = 1;  // TODO
  pthread_mutex_unlock(&mtx);
  return i;
}

static inline void free_socket_cell(int cell) {
  /**
   * free the clients and is_activearray cells
   */

  pthread_mutex_lock(&mtx);
  count_active_clients--;
  close(clients[cell]);
  is_active[cell] = 0;  // TODO
  pthread_mutex_unlock(&mtx);
}

static inline void notify_all(char *buffer, int message_len, int skip) {
  /**
   * send the message to every active client
   */
  int i = 0;
  int sockfd;
  char flag;
  uint32_t len = htonl(message_len);
  for (; i < MAX_COUNT_CLIENTS; ++i) {
    if (i == skip) continue;
    pthread_mutex_lock(&mtx);
    flag = is_active[i];
    sockfd = clients[i];
    pthread_mutex_unlock(&mtx);
    if (flag) {
      fprintf(stdout, "f:%d fd %d", i, sockfd);
      fflush(stdout);
      if (send(sockfd, &len, sizeof(uint32_t), 0) == -1)
        perror("send message len error");

      if (send(sockfd, buffer, message_len, 0) == -1)
        perror("send message error");
    }
  }
}

static void *client_handler(void *arg) {
  /**
   * get message from client and to notify all other clients.
   */
  int cell = *(int *)arg;
  free(arg);
  char nick[256];
  char message[256];
  uint32_t nick_len;
  uint32_t message_len;
  bzero(message, 256);
  bzero(nick, 256);
  pthread_mutex_lock(&mtx);
  int sockfd = clients[cell];
  pthread_mutex_unlock(&mtx);
  int flag;
  while (1) {
    if ((flag = recv(sockfd, &nick_len, sizeof(uint32_t), 0)) <= 0) {
      free_socket_cell(cell);
      perror("ERROR opening socket");
      fprintf(stdout, "recv 1f:%d fd:%d flag:%d\n", (int)ntohl(nick_len),
              sockfd, flag);
      fflush(stdout);
      break;
    }
    fprintf(stdout, "recv 1f:%d fd:%d flag:%d\n", (int)ntohl(nick_len), sockfd,
            flag);
    fflush(stdout);
    if ((flag = recv(sockfd, nick, (int)ntohl(nick_len), 0)) <= 0) {
      free_socket_cell(cell);
      perror("ERROR opening socket");

      break;
    }
    fprintf(stdout, "recv 2f:%d fd:%d flag:%d", (int)ntohl(nick_len), sockfd,
            flag);
    fflush(stdout);
    if ((flag = recv(sockfd, &message_len, sizeof(uint32_t), 0)) <= 0) {
      break;
      perror("ERROR opening socket");
    }
    if (recv(sockfd, message, (int)ntohl(message_len), 0) <= 0) {
      free_socket_cell(cell);
      perror("ERROR opening socket");
      break;
    }
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    printf("<%02d:%02d> [%s]:%s", lt->tm_hour, lt->tm_min, nick, message);
    fflush(stdout);
    notify_all(nick, (int)ntohl(nick_len), cell);
    notify_all(message, (int)ntohl(message_len), cell);
  }
  return NULL;
}
void handdle(int signal) {
  printf("%d", signal);
  return;
}
int main(int argc, char *argv[]) {
  signal(SIGPIPE, handdle);
  int sockfd, newsockfd;
  uint16_t portno;
  unsigned int clilen;
  struct sockaddr_in serv_addr, cli_addr;
  (void)argc;
  (void)argv;

  /* First call to socket() function */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (sockfd < 0) {
    perror("ERROR opening socket");
    return 1;
  }

  if (argc != 2) {
    fprintf(stderr, "usage: %s port\n", argv[0]);
    exit(0);
  }

  portno = (uint16_t)atoi(argv[1]);

  /* Initialize socket structure */
  bzero((char *)&serv_addr, sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  /* Now bind the host address using bind() call.*/
  if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR on binding");
    close(sockfd);
    return 1;
  }

  /* Now start listening for the clients, here process will
   * go in sleep mode and will wait for the incoming connection
   */

  listen(sockfd, 5);
  clilen = sizeof(cli_addr);

  /* Accept actual connection from the client */
  while (1) {
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);

    if (newsockfd < 0) {
      perror("ERROR on accept");
      continue;
    }

    if (count_active_clients + 1 > MAX_COUNT_CLIENTS) {
      perror("Customer limit exceeded");
      close(newsockfd);
      continue;
    }

    int cell = reserve_socket_cell();

    if (cell == -1) {
      perror("Customer limit exceeded");
      close(newsockfd);
      continue;
    }
    pthread_mutex_lock(&mtx);
    clients[cell] = newsockfd;
    pthread_mutex_unlock(&mtx);
    pthread_t thread_id;
    int *cell_pointer = (int *)malloc(sizeof(int));
    *cell_pointer = cell;
    if (pthread_create(&thread_id, NULL, client_handler, cell_pointer) != 0) {
      free(cell_pointer);
      continue;
    }

    pthread_detach(thread_id);
  }

  return 0;
}