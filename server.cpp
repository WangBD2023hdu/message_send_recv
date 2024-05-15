#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>

#include <string.h>
#include <sys/time.h>

#define MAX_COUNT_CLIENTS 100

int clients[MAX_COUNT_CLIENTS];
char is_active[MAX_COUNT_CLIENTS];
int count_active_clients;
char cur_time[30];
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
char *current() {
  memset(cur_time, 0, 30);
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t sec = tv.tv_sec;
  struct tm cur_tm;
  localtime_r((time_t *)&sec, &cur_tm);
  snprintf(cur_time, 30, "%02d:%02d", cur_tm.tm_hour, cur_tm.tm_min);
  return cur_time;
}
static inline int reserve_socket_cell() {
  pthread_mutex_lock(&mtx);
  for (int i = 0; i < MAX_COUNT_CLIENTS; i++) {
    if (!is_active[i]) {
      is_active[i] = 1;
      count_active_clients++;
      pthread_mutex_unlock(&mtx);
      return i;
    }
  }
  pthread_mutex_unlock(&mtx);
  return -1;
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
ssize_t force_send(int sockfd, char *buf, size_t len) {
  size_t index = 0;
  int result;
  while (index < len) {
    result = write(sockfd, buf + index, len - index);
    if (result < 0) {
      return -1;
    }
    index += result;
  }
  return 0;
}

ssize_t force_read(int sockfd, char *buf, size_t len) {
  size_t index = 0;
  int result;
  while (index < len) {
    result = read(sockfd, buf + index, len - index);
    if (result < 0) {
      return -1;
    } else if (result == 0) {
      fprintf(stdout, "server read sockfd is closed\n");
      fflush(stdout);
      return -1;
    }
    index += result;
  }
  return 0;
}

static inline void notify_all(char *buffer, int message_len) {
  /**
   * send the message to every active client
   */
  pthread_mutex_lock(&mtx);
  int i = 0;
  for (; i < MAX_COUNT_CLIENTS; ++i) {
    // if (i == skip) continue;

    if (is_active[i]) {
      fprintf(stdout, "cell:%d fd:%d\n", i, clients[i]);
      fflush(stdout);
      if (send(clients[i], buffer, message_len, 0) <= 0) {
        perror("send message error");
        fprintf(stdout, "send err cell:%d fd:%d\n", i, clients[i]);
        fflush(stdout);
        free_socket_cell(i);
        fprintf(stdout, "release cell:%d fd:%d\n", i, clients[i]);
      }
    }
  }
  pthread_mutex_unlock(&mtx);
}

static void *client_handler(void *arg) {
  /**
   * get message from client and to notify all other clients.
   */
  int cell = *(int *)arg;
  free(arg);
  char nick[256];
  char message[256];
  // char body[256];

  // bzero(body, 256);

  while (1) {
    char nicklenbuffer[4];
    char msglenbuffer[4];
    bzero(message, 256);
    bzero(nick, 256);
    uint32_t nick_len, msg_len;

    if (-1 == force_read(clients[cell], nicklenbuffer, 4)) {
      perror("ERROR opening socket");
      break;
    }
    memcpy(&nick_len, nicklenbuffer, 4);
    fprintf(stdout, "test len: %d\n", ntohl(nick_len));
    if (-1 == force_read(clients[cell], nick, ntohl(nick_len))) {
      perror("ERROR 2 opening socket");
      break;
    }
    fprintf(stdout, "test nick: %s\n", nick);
    fflush(stdout);
    if (-1 == force_read(clients[cell], msglenbuffer, 4)) {
      perror("ERROR 3 opening socket");
      break;
    }
    memcpy(&msg_len, msglenbuffer, 4);
    fprintf(stdout, "test msg len: %d\n", ntohl(msg_len));
    fflush(stdout);
    if (-1 == force_read(clients[cell], message, ntohl(msg_len))) {
      perror("ERROR 4 opening socket");
      break;
    }
    fprintf(stdout, "recv 4f:%d nick: %s mess %s len %d %d\n",
            (int)ntohl(msg_len), nick, message, (int)strlen(nick),
            (int)strlen(message));
    fflush(stdout);
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    fprintf(stdout, "<%02d:%02d> [%s]:%s", lt->tm_hour, lt->tm_min, nick,
            message);
    fflush(stdout);

    char *date = current();
    uint32_t dateSize = strlen(date);
    uint32_t net_dateSize = htonl(dateSize);

    notify_all(nicklenbuffer, 4);
    fprintf(stdout, "nick len send success %d\n", (int)ntohl(nick_len));
    fflush(stdout);
    notify_all(nick, (int)strlen(nick));
    fprintf(stdout, "nick send success\n");
    fflush(stdout);
    notify_all(msglenbuffer, 4);
    notify_all(message, (int)ntohl(msg_len));
    fprintf(stdout, "msg send success\n");
    fflush(stdout);
    notify_all((char *)&net_dateSize, sizeof(net_dateSize));
    notify_all(date, dateSize);
    // notify_all(body, strlen(body));
    // pthread_mutex_unlock(&mtx);
    fprintf(stdout, "data send success\n");
    fflush(stdout);
  }
  free_socket_cell(cell);
  return NULL;
}

int main(int argc, char *argv[]) {
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
    if (pthread_create(&thread_id, NULL, client_handler,
                       (void *)cell_pointer) != 0) {
      free(cell_pointer);
      continue;
    }

    //
  }

  return 0;
}