/* Katarzyna Szmagara 332171 */
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define PORT 54322
#define min(a, b) ((a) < (b) ? (a) : (b))
#define MAX_DATA_SIZE 1000
#define WINDOW_SIZE 1000

typedef struct Node {
  char *data;
  int size;
  struct Node *next;
} Node;

char *new_message(int start, int length) {
  char *message = (char *)malloc(30 * sizeof(char));
  if (message == NULL) {
    perror("Allocation failed");
    exit(EXIT_FAILURE);
  }

  sprintf(message, "GET %d %d\n", start, length);
  return message;
}

bool send_request(const char *ip_address, int server_port, const char *message,
                  int *sockfd) {
  struct sockaddr_in server_addr;

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
    perror("inet_pton failed");
    return false;
  }

  if (sendto(*sockfd, message, strlen(message), 0,
             (const struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
    perror("sendto failed");
    return false;
  }

  return true;
}

bool write_to_file(const char *filename, const char *data, int size) {
  FILE *file = fopen(filename, "ab");
  if (file == NULL) {
    perror("Error opening file");
    return false;
  }

  fwrite(data, sizeof(char), size, file);

  fclose(file);
  return true;
}

void print_window(Node *window) {
  printf("Window contents:\n");
  int idx = 0;
  Node *current = window;
  while (current != NULL) {
    printf("Node %d: ", idx);
    if (current->data != NULL) {
      printf("%s\n", current->data);
    } else {
      printf("Empty\n");
    }
    current = current->next;
    idx++;
  }
  printf("\n");
}

int handle_data(char *sender, char *message, const char *ip_address, int start,
                const char *filename, Node **window) {
  struct sockaddr_in sa_sender, sa_ip_address;
  inet_pton(AF_INET, sender, &(sa_sender.sin_addr));
  inet_pton(AF_INET, ip_address, &(sa_ip_address.sin_addr));

  if (memcmp(&sa_sender.sin_addr, &sa_ip_address.sin_addr,
             sizeof(struct in_addr)) == 0) {
    char *data_start = strchr(message, '\n');
    if (data_start != NULL) {
      data_start++;
      int s, l;
      if (sscanf(message, "DATA %d %d", &s, &l) == 2) {
        int idx = (s - start) / MAX_DATA_SIZE;

        if (idx >= 0 && idx < WINDOW_SIZE) {
          if (*window == NULL) {
            *window = (Node *)malloc(sizeof(Node));
            (*window)->next = NULL;
            (*window)->data = NULL;
            (*window)->size = 0;
          }
          Node *temp = *window;

          Node *prev = NULL;
          for (int i = 0; i <= idx; i++) {
            if (temp == NULL) {
              temp = (Node *)malloc(sizeof(Node));
              temp->next = NULL;
              temp->data = NULL;
              temp->size = 0;
            }
            if (prev)
              prev->next = temp;
            else
              (*window) = temp;
            prev = temp;
            temp = temp->next;
          }

          if (prev) {
            prev->data = (char *)malloc(l + 1);
            memcpy(prev->data, data_start, l);
            prev->size = l;
          } else {
            (*window)->data = (char *)malloc(l + 1);
            memcpy((*window)->data, data_start, l);
            (*window)->size = l;
          }
        }

        if (idx == 0) {
          int done = 0;
          int bytes = 0;
          while (*window && (*window)->size) {
            Node *temp = *window;
            if (write_to_file(filename, temp->data, temp->size)) {
              bytes += temp->size;
              *window = temp->next;
              free(temp->data);
              free(temp);
              done++;
            } else {
              perror("Error writing to file");
              return false;
            }
          }

          return bytes;
        }
      }
    }
  }
  return false;
}

bool receive(int sockfd, char *sender, char *buffer) {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;

  int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
  if (select_result == -1) {
    perror("select failed");
    return false;
  } else if (select_result == 0) {
    return false;
  }

  if (!FD_ISSET(sockfd, &read_fds))
    return false;

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received =
        recvfrom(sockfd, buffer, BUF_SIZE, MSG_DONTWAIT,
                 (struct sockaddr *)&client_addr, &client_len);
    if (bytes_received < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN)
        break;
      perror("recvfrom failed");
      return false;
    } else if (bytes_received == 0) {
      printf("Connection closed by peer\n");
      return false;
    }

    strcpy(sender, inet_ntoa(client_addr.sin_addr));

    return true;
  }
  return false;
}

int create_socket() {
  int sockfd;
  struct sockaddr_in server_addr;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  return sockfd;
}

void transport(const char *ip_address, int server_port, char *filename,
               int start, int size) {
  int sockfd = create_socket();
  Node *first = NULL;
  Node **window = &first;
  while (start < size - 1) {
    Node *temp = *window;
    for (int i = 0; i < WINDOW_SIZE && i * MAX_DATA_SIZE + start < size; i++) {
      int new_start = i * MAX_DATA_SIZE + start;
      int length = min(MAX_DATA_SIZE, size - new_start);
      int ssss = false;
      if (temp == NULL) {
        ssss = true;
        send_request(ip_address, server_port, new_message(new_start, length),
                     &sockfd);
      } else {
        if (temp->size == 0) {
          ssss = true;
          send_request(ip_address, server_port, new_message(new_start, length),
                       &sockfd);
        }
        temp = temp->next;
      }
      if (!ssss && start == i)
        printf("kwa\n");
    }

    char sender[100];
    char *message = malloc((MAX_DATA_SIZE + 30) * sizeof(char));
    while (receive(sockfd, sender, message)) {
      int res =
          handle_data(sender, message, ip_address, start, filename, window);
      start += res;
      if (res) {
        printf("%lf%% done\n", (double)start / (double)size * 100);
      }
      message = malloc((MAX_DATA_SIZE + 30) * sizeof(char));
    }
    free(message);
  }
  while (*window) {
    Node *temp = *window;
    free((*window)->data);
    *window = (*window)->next;
    free(temp);
  }
}

bool isValidIPAddress(const char *ipAddress) {
  struct sockaddr_in sa;
  return inet_pton(AF_INET, ipAddress, &(sa.sin_addr)) != 0;
}

bool isValidPort(const char *port) {
  int num = atoi(port);
  return num > 0 && num < 65536;
}

bool isValidSize(const char *size) {
  int num = atoi(size);
  return num > 0 && num <= 10000000;
}

bool validate_argv(int argc, char *argv[]) {
  if (argc != 5) {
    printf("Użycie: %s <adres_IP> <port> <nazwa_pliku> <rozmiar>\n", argv[0]);
    return false;
  }

  if (!isValidIPAddress(argv[1])) {
    printf("Błędny adres IP.\n");
    return false;
  }

  if (!isValidPort(argv[2])) {
    printf("Błędny numer portu.\n");
    return false;
  }

  if (!isValidSize(argv[4])) {
    printf("Błędny rozmiar.\n");
    return false;
  }
  return true;
}

int main(int argc, char *argv[]) {
  if (!validate_argv(argc, argv))
    return 1;
  char ip_address[20];
  strcpy(ip_address, argv[1]);
  int server_port = atoi(argv[2]);
  char filename[20];
  strcpy(filename, argv[3]);
  int size = atoi(argv[4]);
  transport(ip_address, server_port, filename, 0, size);
}