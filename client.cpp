#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>

#include <string.h>

#include <time.h>

pthread_mutex_t input_mode_mtx = PTHREAD_MUTEX_INITIALIZER;

char is_input_mode;

char force_read(int sockfd, char *buffer, int len) {
  int nLen;
  nLen = (int)recv(sockfd, buffer, len, 0);
  if (nLen <= 0) {
    perror("<socket>is closed\n");
    return '0';
  }
  return (char)nLen;
}

char read_message(int sockfd_, char *buffer) {
  char len;

  if ('0' == force_read(sockfd_, &len, sizeof(char))) {
    perror("read head failure");
    return '0';
  }
  if ('0' == force_read(sockfd_, buffer, (int)len)) {
    perror("read data failure");
    return '0';
  }
  return '1';
}

static void *server_handler(void *arg) {
  int sockfd_ = *(int *)arg;
  char nick[256] = {};
  char message[256] = {};

  bzero(message, 256);
  while (1) {
    if ('0' == read_message(sockfd_, nick)) {
      perror("socket is closed");
      break;
    }
    if ('0' == read_message(sockfd_, message)) {
      perror("socket is closed");
      break;
    }
    pthread_mutex_lock(&input_mode_mtx);
    char flag = is_input_mode;
    pthread_mutex_unlock(&input_mode_mtx);
    while (flag) {
      sleep(1);
      pthread_mutex_lock(&input_mode_mtx);
      flag = is_input_mode;
      pthread_mutex_unlock(&input_mode_mtx);
    }
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    printf("<%02d:%02d> [%s]:%s", lt->tm_hour, lt->tm_min, nick, message);
  }
  return NULL;
}

char force_send(int sockfd, char *buffer, int len) {
  if (-1 == send(sockfd, buffer, len, 0)) {
    return 'F';
  } else {
    return 'T';
  }
}

char send_message(int sockfd, char *nickname, char *text) {
  char nick_len = strlen(nickname) + 1;
  char text_len = strlen(text) + 1;
  if ('F' == force_send(sockfd, &nick_len, sizeof(char))) {
    fprintf(stdout,"send 1 false");
    return '0';
  }
  if ('F' == force_send(sockfd, nickname, strlen(nickname) + 1)) {
    fprintf(stdout,"send 1 false");
    return '0';
  }
  if ('F' == force_send(sockfd, &text_len, sizeof(char))) {
    fprintf(stdout,"send 1 false");
    return '0';
  }
  if ('F' == force_send(sockfd, text, strlen(text) + 1)) {
    fprintf(stdout,"send 1 false");
    return '0';
  }

  return '1';  // send message sucess
}

int main(int argc, char *argv[]) {
  int sockfd = 0;
  char *nickname = NULL;
  uint16_t portno = 0;
  struct sockaddr_in serv_addr = {};
  struct hostent *server = NULL;

  char buffer[256] = {};

  if (argc != 4) {
    fprintf(stderr, "usage: %s hostname port nickname\n", argv[0]);
    exit(0);
  }

  portno = (uint16_t)atoi(argv[2]);

  /* Create a socket point */

  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  if (sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }

  server = gethostbyname(argv[1]);

  if (server == NULL) {
    fprintf(stderr, "ERROR, no such host\n");
    exit(0);
  }

  nickname = argv[3];

  if (strlen(nickname) > 40) {
    fprintf(stderr, "ERROR, nickname very long\n");
    exit(1);
  }

  bzero((char *)&serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy(server->h_addr, (char *)&serv_addr.sin_addr.s_addr,
        (size_t)server->h_length);
  serv_addr.sin_port = htons(portno);

  /* Now connect to the server */
  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR connecting");
    exit(1);
  }

  /* Now ask for a message from the user, this message
   * will be read by server
   */
  pthread_t thread_id;
  if (pthread_create(&thread_id, NULL, server_handler, &sockfd) != 0) {
    perror("ERROR thread create");
    exit(1);
  }

  while (1) {
    bzero(buffer, 256);
    fgets(buffer, 200, stdin);
    while (strcmp(buffer, "m\n") != 0) {
      if (strcmp(buffer, "exit\n") == 0) {
        close(sockfd);
        return 0;
      }

      printf("Invalid input\n");
      bzero(buffer, 256);
      fgets(buffer, 200, stdin);
    }

    pthread_mutex_lock(&input_mode_mtx);
    is_input_mode = 1;
    pthread_mutex_unlock(&input_mode_mtx);

    printf("Please enter the message: ");
    bzero(buffer, 256);
    fgets(buffer, 200, stdin);

    is_input_mode = 0;

    /* Send message to the server */

    if (!send_message(sockfd, nickname, buffer)) {
      perror("ERROR writing to socket");
      exit(1);
    }
  }

  return 0;
}
