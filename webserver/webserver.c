/* Katarzyna Szmagara 332171 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h> 
#include <sys/select.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

void parse_get_request(char *request, char *path, char *host,
                       char *connection) {
  char *line;
  char *saveptr;

  line = strtok_r(request, "\n", &saveptr);
  char *method = strtok(line, " ");
  char *path_proto = strtok(NULL, " ");
  char *protocol = strtok(NULL, " ");
  strcpy(path, path_proto);

  while ((line = strtok_r(NULL, "\n", &saveptr)) != NULL) {
    if (strcmp(line, "") == 0 || strcmp(line, "\n") == 0 ||
        strcmp(line, "\n") == 0) {
      break;
    }
    char *key = strtok(line, ": ");
    char *value = strtok(NULL, "\n");
    if (strcmp(key, "Host") == 0) {
      char *colon_pos = strchr(value, ':');
      if (colon_pos != NULL) {
        *colon_pos = '\0';
      }
      strcpy(host, value + 1);
    } else if (strcmp(key, "Connection") == 0) {
      strcpy(connection, value);
    }
  }
}

char *create_full_path(char *directory, char *get_request, char* path) {
  char host[256];
  char connection[256];

  parse_get_request(get_request, path, host, connection);

  size_t full_path_length = strlen(directory) + strlen(path) + strlen(host) + 2;
  char *full_path = (char *)malloc(full_path_length);
  if (!full_path) {
    perror("malloc");
    exit(1);
  }

  snprintf(full_path, full_path_length, "%s%s%s", directory, host, path);

  return full_path;
}

const char *get_content_type(const char *path) {
  const char *ext = strrchr(path, '.');
  if (!ext)
    return "application/octet-stream";
  ext++;
  if (strcmp(ext, "txt") == 0) {
    return "text/plain";
  } else if (strcmp(ext, "html") == 0) {
    return "text/html; charset=utf-8";
  } else if (strcmp(ext, "css") == 0) {
    return "text/css";
  } else if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) {
    return "image/jpeg";
  } else if (strcmp(ext, "png") == 0) {
    return "image/png";
  } else if (strcmp(ext, "pdf") == 0) {
    return "application/pdf";
  } else {
    return "application/octet-stream";
  }
}

void handle_501(int client_socket) {
  const char *response = "HTTP/1.1 501 Not Implemented\nContent-Type: "
                           "text/plain\n\n501 Not Implemented";
    ssize_t bytes_sent = send(client_socket, response, strlen(response), 0);
    if (bytes_sent < 0) {
      perror("send");
    }
}

void handle_404(int client_socket) {
    if (errno == ENOENT) {
      const char *response = "HTTP/1.1 404 Not Found\nContent-Type: "
                             "text/plain\n\n404 Not Found";
      ssize_t bytes_sent = send(client_socket, response, strlen(response), 0);
      if (bytes_sent < 0) {
        perror("send");
      }
    } else {
      perror("fopen");
    }
    close(client_socket);
    return;
}

void handle_301(int client_socket, char *redirect_url) {
  char response_header[512];
  snprintf(response_header, sizeof(response_header),
           "HTTP/1.1 301 Moved Permanently\nLocation: %sindex.html\n\n",
           redirect_url);

  ssize_t header_len = strlen(response_header);
  ssize_t bytes_sent = send(client_socket, response_header, header_len, 0);
  if (bytes_sent < 0) {
    perror("send");
    exit(1);
  }
}

void handle_403(int client_socket) {
  const char *response = "HTTP/1.1 403 Forbidden\nContent-Type: "
                           "text/plain\n\n403 Forbidden";
  ssize_t bytes_sent = send(client_socket, response, strlen(response), 0);
  if (bytes_sent < 0) {
    perror("send");
  }
}

void handle_client_request(int client_socket, char *directory) {
  char buffer[BUFFER_SIZE];
  char dir2[256];
  strcpy(dir2, directory);
  ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
  if (bytes_received < 0) {
    perror("recv");
    exit(1);
  }

  buffer[bytes_received] = '\0';

  if (strncmp("GET", buffer, 3) != 0) {
    handle_501(client_socket);
    return;
  }

  char path[256];
  char *full_path = create_full_path(directory, buffer, path);

  if (strncmp(directory, full_path, strlen(directory)) != 0) {
    handle_403(client_socket); 
    free(full_path);
    return;
  }

  struct stat path_stat;
  stat(full_path, &path_stat);
  if (S_ISDIR(path_stat.st_mode)) {
    handle_301(client_socket, path);
    free(full_path);
    return;
  }

  FILE *file = fopen(full_path, "r");
  if (file == NULL) {
    handle_404(client_socket);
    free(full_path);
    return;
  }

  fseek(file, 0, SEEK_END);
  size_t file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *file_contents = (char *)malloc(file_size);
  if (!file_contents) {
    perror("malloc");
    exit(1);
  }

  fread(file_contents, 1, file_size, file);

  fclose(file);

  const char *content_type = get_content_type(full_path);
  char response_header[512];
  snprintf(response_header, sizeof(response_header),
           "HTTP/1.1 200 OK\nContent-Type: %s\n\n", content_type);

  ssize_t header_len = strlen(response_header);
  ssize_t bytes_sent = send(client_socket, response_header, header_len, 0);
  if (bytes_sent < 0) {
    perror("send");
    exit(1);
  }

  bytes_sent = send(client_socket, file_contents, file_size, 0);
  if (bytes_sent < 0) {
    perror("send");
    exit(1);
  }

  free(full_path);
  free(file_contents);
  close(client_socket);
}

void set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl");
    exit(1);
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl");
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port> <directory>\n", argv[0]);
    return 1;
  }

  int port = atoi(argv[1]);
  char *directory = argv[2];

  if (access(directory, F_OK) == -1) {
    perror("access");
    return 1;
  }

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    perror("socket");
    return 1;
  }

  struct sockaddr_in server_address, client_address;
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr *)&server_address,
           sizeof(server_address)) < 0) {
    perror("bind");
    return 1;
  }

  if (listen(server_socket, 5) < 0) {
    perror("listen");
    return 1;
  }

  printf("Server listening on port %d...\n", port);

  fd_set readfds, masterfds;
  FD_ZERO(&masterfds);
  FD_SET(server_socket, &masterfds);
  int max_sd = server_socket;

  while (1) {
    readfds = masterfds;
    int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
    if ((activity < 0) && (errno != EINTR)) {
      perror("select");
      exit(1);
    }

    if (FD_ISSET(server_socket, &readfds)) {
      socklen_t client_address_len = sizeof(client_address);
      int client_socket =
          accept(server_socket, (struct sockaddr *)&client_address,
                 &client_address_len);
      if (client_socket < 0) {
        perror("accept");
        continue;
      }
      set_non_blocking(client_socket);
      FD_SET(client_socket, &masterfds);
      if (client_socket > max_sd) {
        max_sd = client_socket;
      }
    }

    for (int i = 0; i <= max_sd; ++i) {
      if (FD_ISSET(i, &readfds)) {
        if (i != server_socket) {
          handle_client_request(i, directory);
          close(i);
          FD_CLR(i, &masterfds);
        }
      }
    }
  }

  close(server_socket);

  return 0;
}