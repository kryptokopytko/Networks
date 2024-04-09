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
#define IP_ADDR_LENGTH 16
#define MAX_TABLE_LENGTH 20
#define INF_DIST 0xFFFFFFFF
#define SERVER_PORT 54321

struct routing_record {
  char address[IP_ADDR_LENGTH];
  uint32_t mask;
  uint32_t distance;
  char via[IP_ADDR_LENGTH];
};

struct direct_network {
  char address[IP_ADDR_LENGTH];
  uint32_t mask;
  uint32_t distance;
};

struct neighbour {
  char address[IP_ADDR_LENGTH];
  uint32_t distance;
  uint32_t rounds_since_responded;
};

struct neighbour neighbours[MAX_TABLE_LENGTH];
uint32_t number_of_neighbours = 0;

struct direct_network direct_networks[MAX_TABLE_LENGTH];
uint32_t number_of_direct_networks = 0;

struct routing_record table[MAX_TABLE_LENGTH];
uint32_t table_length = 0;

void struct_to_string(struct routing_record record, char *message) {
  snprintf(message, BUF_SIZE, "%s/%d distance %d", record.address, record.mask,
           record.distance);
}

bool are_the_same_addr_of_network(const char *ip1, const char *ip2,
                                  size_t mask) {
  uint32_t ip1_num[4], ip2_num[4];
  sscanf(ip1, "%d.%d.%d.%d", &ip1_num[0], &ip1_num[1], &ip1_num[2],
         &ip1_num[3]);
  sscanf(ip2, "%d.%d.%d.%d", &ip2_num[0], &ip2_num[1], &ip2_num[2],
         &ip2_num[3]);
  uint32_t num1, num2;
  num1 =
      (ip1_num[0] << 24) + (ip1_num[1] << 16) + (ip1_num[2] << 8) + ip1_num[3];
  num2 =
      (ip2_num[0] << 24) + (ip2_num[1] << 16) + (ip2_num[2] << 8) + ip2_num[3];

  uint32_t netmask = (0xFFFFFFFF << (32 - mask));
  return ((num1 & netmask) == (num2 & netmask));
}
void handle_configuration(char *message) {
  char address[IP_ADDR_LENGTH];
  uint32_t mask;
  uint32_t distance;

  sscanf(message, "%[^/]/%d distance %d", address, &mask, &distance);

  address[IP_ADDR_LENGTH - 1] = '\0';

  struct routing_record new_record;
  strcpy(new_record.address, address);
  new_record.mask = mask;
  new_record.distance = distance;
  strcpy(new_record.via, "directly");
  table[table_length] = new_record;
  table_length++;

  struct direct_network new_network;
  strcpy(new_network.address, address);
  new_network.mask = mask;
  new_network.distance = distance;
  direct_networks[number_of_direct_networks] = new_network;
  number_of_direct_networks++;
}

void create_routing_entry(char *address, uint32_t mask, uint32_t distance,
                          char *via, struct routing_record *table,
                          uint32_t table_length) {
  struct routing_record new_record;
  strcpy(new_record.address, address);
  new_record.mask = mask;
  new_record.distance = distance;
  strcpy(new_record.via, via);
  table[table_length] = new_record;
}

void print_table() {
  printf("Routing Table:\n");
  for (uint32_t i = 0; i < table_length; i++) {
    if (strcmp(table[i].via, "directly"))
      printf("%s/%d distance: %d via: %s\n", table[i].address, table[i].mask,
             table[i].distance, table[i].via);
    else
      printf("%s/%d distance: %d connected directly\n", table[i].address,
             table[i].mask, table[i].distance);
  }
}

void print_neighbours() {
  printf("Neighbour Table:\n");
  for (uint32_t i = 0; i < number_of_neighbours; i++) {
    printf("Address: %s, Distance: %d, rounds: %d\n", neighbours[i].address,
           neighbours[i].distance, neighbours[i].rounds_since_responded);
  }
}

void print_direct_networks() {
  printf("Direct Network Table:\n");
  for (uint32_t i = 0; i < number_of_direct_networks; i++) {
    printf("address: %s/%d, Distance: %d\n", direct_networks[i].address,
           direct_networks[i].mask, direct_networks[i].distance);
  }
}

void delete_table_entry(uint32_t id) {
  for (uint32_t i = id; i < table_length - 1; i++)
    table[i] = table[i + 1];
  table_length--;
}

void handle_routing_entry(char *sender, char *message) {
  char address[IP_ADDR_LENGTH];
  uint32_t mask;
  uint32_t distance;
  sscanf(message, "%[^/]/%d distance %d", address, &mask, &distance);
  uint32_t idx_of_sender;

  for (uint32_t i = 0; i < number_of_direct_networks; i++)
    if (strcmp(direct_networks[i].address, sender) == 0)
      return;

  for (idx_of_sender = 0; idx_of_sender < number_of_neighbours; idx_of_sender++)
    if (strcmp(neighbours[idx_of_sender].address, sender) == 0)
      break;

  uint32_t idx_of_address;
  for (idx_of_address = 0; idx_of_address < table_length; idx_of_address++)
    if (mask == table[idx_of_address].mask &&
        are_the_same_addr_of_network(table[idx_of_address].address, address,
                                     mask))
      break;

  uint32_t idx_of_direct;
  for (idx_of_direct = 0; idx_of_direct < number_of_direct_networks;
       idx_of_direct++)
    if (mask == table[idx_of_direct].mask &&
        are_the_same_addr_of_network(table[idx_of_direct].address, address,
                                     mask))
      break;

  if (idx_of_sender == number_of_neighbours) {
    if (idx_of_address < table_length &&
        strcmp(table[idx_of_address].via, "directly") == 0) {
      struct neighbour newn = {"", distance, .rounds_since_responded = 0};
      strcpy(newn.address, sender);
      neighbours[number_of_neighbours] = newn;
      number_of_neighbours++;
    }
    return;
  }

  if (neighbours[idx_of_sender].distance == INF_DIST) {
    if (idx_of_address < table_length &&
        strcmp(table[idx_of_address].via, "directly") == 0)
      neighbours[idx_of_sender].distance = distance;
    return;
  }

  neighbours[idx_of_sender].rounds_since_responded = 0;
  uint32_t new_distance =
      neighbours[idx_of_sender].distance == INF_DIST
          ? INF_DIST
          : (distance == INF_DIST
                 ? INF_DIST
                 : neighbours[idx_of_sender].distance + distance);

  if (idx_of_address == table_length) {
    create_routing_entry(address, mask, new_distance, sender, table,
                         table_length);
    table_length++;
    return;
  }
  if (table[idx_of_address].distance > new_distance ||
      strcmp(table[idx_of_address].via, sender) == 0) {
    table[idx_of_address].distance = new_distance;
    strcpy(table[idx_of_address].via, sender);
    if (new_distance > 16)
      delete_table_entry(idx_of_address);
  }
  if (idx_of_direct < number_of_direct_networks &&
      table[idx_of_address].distance >
          direct_networks[idx_of_direct].distance) {
    table[idx_of_address].distance = direct_networks[idx_of_direct].distance;
    strcpy(table[idx_of_address].via, "direct");
  }
  return;
}

char *calculate_broadcast_address(const char *ip_address, uint32_t mask) {
  struct in_addr addr;

  if (inet_pton(AF_INET, ip_address, &addr) != 1) {
    fprintf(stderr, "Parsing IP address error.\n");
    return NULL;
  }

  uint32_t network_addr = ntohl(addr.s_addr) & (~((1 << (32 - mask)) - 1));
  uint32_t broadcast_addr = network_addr | ((1 << (32 - mask)) - 1);
  struct in_addr broadcast_in_addr;
  broadcast_in_addr.s_addr = htonl(broadcast_addr);
  char *broadcast_str = (char *)malloc(INET_ADDRSTRLEN * sizeof(char));
  if (inet_ntop(AF_INET, &broadcast_in_addr, broadcast_str, INET_ADDRSTRLEN) ==
      NULL) {
    fprintf(stderr, "Error with computing broadcast address.\n");
    free(broadcast_str);
    return NULL;
  }

  return broadcast_str;
}

bool send_udp_broadcast(const char *ip_address, uint32_t mask,
                        const char *message, int *sockfd) {
  struct sockaddr_in server_addr;

  char *broadcast_ip = calculate_broadcast_address(ip_address, mask);
  if (broadcast_ip == NULL) {
    fprintf(stderr, "Error calculating broadcast address.\n");
    return false;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, broadcast_ip, &server_addr.sin_addr) <= 0) {
    perror("inet_pton failed");
    free(broadcast_ip);
    return false;
  }

  if (sendto(*sockfd, message, strlen(message), 0,
             (const struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
    perror("sendto failed");
    free(broadcast_ip);
    return false;
  }

  free(broadcast_ip);
  return true;
}

void send_table(int *sockfd) {
  for (uint32_t i = 0; i < number_of_direct_networks; i++) {
    char broadcast_ip[IP_ADDR_LENGTH];
    strcpy(broadcast_ip, calculate_broadcast_address(direct_networks[i].address,
                                                     direct_networks[i].mask));
    for (uint32_t j = 0; j < table_length; j++) {
      char message[BUF_SIZE];
      struct_to_string(table[j], message);
      int is_fail = send_udp_broadcast(broadcast_ip, direct_networks[i].mask,
                                       message, sockfd);
      if (is_fail == -1) {
        for (uint32_t n = 0; n < number_of_neighbours; n++)
          if (are_the_same_addr_of_network(neighbours[n].address,
                                           direct_networks[i].address,
                                           direct_networks[i].mask))
            neighbours[n].rounds_since_responded = INF_DIST;
      }
    }
  }
}

void receive(int sockfd) {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
  if (select_result == -1) {
    perror("select failed");
    return;
  } else if (select_result == 0) {
    return;
  }

  if (!FD_ISSET(sockfd, &read_fds))
    return;

  while (1) {
    char buffer[BUF_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received =
        recvfrom(sockfd, buffer, BUF_SIZE, MSG_DONTWAIT,
                 (struct sockaddr *)&client_addr, &client_len);
    if (bytes_received < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN)
        break;
      perror("recvfrom failed");
      return;
    } else if (bytes_received == 0) {
      printf("Connection closed by peer\n");
      return;
    }

    buffer[bytes_received] = '\0';
    handle_routing_entry(inet_ntoa(client_addr.sin_addr), buffer);
  }
}

void handle_unavailable_neighbour(uint32_t id) {
  neighbours[id].distance = INF_DIST;
  for (uint32_t i = 0; i < table_length; i++)
    if (strcmp(table[i].via, neighbours[id].address) == 0)
      table[i].distance = INF_DIST;
}

void loop(int sockfd) {
  while (1) {
    for (uint32_t i = 0; i < number_of_neighbours; i++)
      neighbours[i].rounds_since_responded++;
    receive(sockfd);
    send_table(&sockfd);
    print_table();
    // print_neighbours();
    for (uint32_t i = 0; i < number_of_neighbours; i++)
      if (neighbours[i].rounds_since_responded > 2)
        handle_unavailable_neighbour(i);

    sleep(3);
  }
}

int create_socket() {
  int sockfd;
  struct sockaddr_in server_addr;
  int broadcast_permission = 1;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_permission,
                 sizeof(broadcast_permission)) == -1) {
    perror("setsockopt failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(SERVER_PORT);

  if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  return sockfd;
}

void input() {
  char line[BUF_SIZE];
  int num_interfaces;

  if (fgets(line, sizeof(line), stdin) != NULL) {
    sscanf(line, "%d", &num_interfaces);
  } else {
    fprintf(stderr, "Error reading number of interfaces.\n");
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < num_interfaces; ++i) {
    if (fgets(line, sizeof(line), stdin) != NULL) {
      handle_configuration(line);
    } else {
      fprintf(stderr, "Error reading interface configuration.\n");
      exit(EXIT_FAILURE);
    }
  }
}

int main() {
  int sockfd = create_socket();
  input();
  loop(sockfd);
  close(sockfd);
}
