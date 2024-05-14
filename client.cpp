#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>

#include <string.h>

#include <sys/socket.h>
#include <time.h>
pthread_mutex_t input_mode_mtx = PTHREAD_MUTEX_INITIALIZER;

char is_input_mode;

char force_read(int sockfd, char *buffer, int len) {
  int nLen;
  nLen = (int)recv(sockfd, buffer, (size_t)len, 0);
  if (nLen <= 0) {
    perror("<socket>is closed\n");
    return '0';
  }
  return (char)nLen;
}

// ssize_t force_read(int sockfd, char *buf, size_t len) {
//   for (size_t index = 0; index < len;) {
//     int result = recv(sockfd, buf + index, len - index, 0);
//     if (result <= 0) {
//       return -1;
//     }
//     index += result;
//   }
//   return 0;
// }

char read_message(int sockfd_, char *buffer) {
  uint32_t len;

  if ('0' == force_read(sockfd_, (char *)&len, sizeof(uint32_t))) {
    perror("read head failure");
    return '0';
  }
  if ('0' == force_read(sockfd_, buffer, (size_t)ntohl(len))) {
    perror("read data failure");
    return '0';
  }
  return '1';
}

static void *server_handler(void *arg) {
  int sockfd_ = *(int *)arg;
  free(arg);
  char nick[256];
  char message[256];
  // char body[256];
  bzero(nick, 256);
  bzero(message, 256);
  // bzero(body, 256);
  int whi = 1;
  while (whi) {
    if ('0' == read_message(sockfd_, nick)) {
      perror("socket is closed");
      break;
    }
    if ('0' == read_message(sockfd_, message)) {
      perror("socket is closed");
      break;
    }
    // if ('0' == read_message(sockfd_, body)) {
    //   perror("socket is closed");
    //   break;
    // }
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
    fprintf(stdout, "<%02d:%02d> [%s]:%s", lt->tm_hour, lt->tm_min, nick,
            message);
    fflush(stdout);
    whi = 0;
  }
  close(sockfd_);
  return NULL;
}

char force_send(int sockfd, char *buffer, int len) {
  if (-1 == send(sockfd, buffer, (size_t)len, 0)) {
    return 'F';
  } else {
    return 'T';
  }
}
// ssize_t force_send(int sockfd, char *buf, size_t len) {
//   for (size_t index = 0; index < len;) {
//     int result = send(sockfd, buf + index, len - index, 0);
//     if (result <= 0) {
//       return -1;
//     }
//     index += result;
//   }
//   return 0;
// }

char send_message(int sockfd, char *nickname, char *text) {
  uint32_t nick_len = htonl((strlen(nickname)));
  uint32_t text_len = htonl((strlen(text)));
  if ('F' == force_send(sockfd, (char *)&nick_len, sizeof(uint32_t))) {
    return '0';
  }
  if ('F' == force_send(sockfd, nickname, strlen(nickname))) {
    return '0';
  }
  if ('F' == force_send(sockfd, (char *)&text_len, sizeof(uint32_t))) {
    return '0';
  }
  if ('F' == force_send(sockfd, text, strlen(text))) {
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
  int *fd = (int *)malloc(sizeof(int));
  *fd = sockfd;
  pthread_t thread_id;
  if (pthread_create(&thread_id, NULL, server_handler, fd) != 0) {
    perror("ERROR thread create");
    free(fd);
    exit(1);
  }
  char flag = 1;
  while (1) {
    bzero(buffer, 256);
    fgets(buffer, 200, stdin);
    while (strcmp(buffer, "m\n") != 0) {
      if (strcmp(buffer, "exit\n") == 0) {
        close(sockfd);
        return 0;
      }
      flag = 0;
      break;
      printf("Invalid input\n");
      bzero(buffer, 256);
      fgets(buffer, 200, stdin);
    }
    if (flag == 0) {
      break;
    }
    pthread_mutex_lock(&input_mode_mtx);
    is_input_mode = 1;
    pthread_mutex_unlock(&input_mode_mtx);

    printf("Please enter the message: ");
    bzero(buffer, 256);
    fgets(buffer, 200, stdin);

    pthread_mutex_lock(&input_mode_mtx);
    is_input_mode = 0;
    pthread_mutex_unlock(&input_mode_mtx);
    /* Send message to the server */

    if (!send_message(sockfd, nickname, buffer)) {
      perror("ERROR writing to socket");
      exit(1);
    }
  }

  return 0;
}