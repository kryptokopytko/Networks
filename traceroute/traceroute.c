#define _POSIX_C_SOURCE 199309L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <netinet/ip_icmp.h>
#include <time.h>
#include <sys/select.h>
#include <strings.h>


u_int16_t compute_icmp_checksum(const void *buff, int length) {
    const u_int16_t *ptr = buff;
    u_int32_t sum = 0;
    assert(length % 2 == 0);
    for (; length > 0; length -= 2)
        sum += *ptr++;
    sum = (sum >> 16U) + (sum & 0xffff);
    return (u_int16_t)(~(sum + (sum >> 16U)));
}

bool isValidIPAddress(char *ipAddress) {
    int num = 0, dotCount = 0, i;
    for (i = 0; ipAddress[i] != '\0'; i++) {
        if (ipAddress[i] >= '0' && ipAddress[i] <= '9') {
            num = num * 10 + (ipAddress[i] - '0');
            if (num < 0 || num > 255)
                return false;
        } else if (ipAddress[i] == '.') {
            if (num > 255)
                return false;
            dotCount++;
            num = 0;
        } else {
            return false;
        }
    }
    if (dotCount != 3 || ipAddress[i - 1] == '.')
        return false;

    return true;
}

void print_as_bytes(unsigned char *buff, ssize_t length) {
    for (ssize_t i = 0; i < length; i++, buff++)
        printf("%.2x ", *buff);
}

int round_average(double array[], int size) {
    double sum = 0.0;
    for (int i = 0; i < size; ++i)
        sum += array[i];

    double average = sum / size;
    int rounded_average = (int)(average + 0.5);
    return rounded_average;
}

double sendPacket(int sockfd, int seqNum, int ttl, char* targetIpAddr, struct timespec *start) {
    // Tworzenie danych do wysyłki
    struct icmphdr header;
    header.type = ICMP_ECHO;
    header.code = 0;
    header.un.echo.id = getpid() & 0xFFFF;
    header.un.echo.sequence = seqNum;
    header.checksum = 0;
    header.checksum = compute_icmp_checksum((u_int16_t *)&header, sizeof(header));

    // Adresowanie
    struct sockaddr_in recipient;
    bzero(&recipient, sizeof(recipient));
    recipient.sin_family = AF_INET;
    inet_pton(AF_INET, targetIpAddr, &recipient.sin_addr);

    // Zmiana TTL
    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(int)) < 0) {
        perror("setsockopt failed");
        return -1;
    }

    // Wysyłanie pakietu
    clock_gettime(CLOCK_MONOTONIC, start);
    ssize_t bytes_sent = sendto(
        sockfd,
        &header,
        sizeof(header),
        0,
        (struct sockaddr *)&recipient,
        sizeof(recipient));
    if (bytes_sent < 0) {
        perror("sendto failed");
        return -1;
    }

    return start->tv_sec * 1000.0 + start->tv_nsec / 1000000.0;
}

double receivePacket(int sockfd, char* responseIpAddr, int* response_type, struct timespec *start, int seqNum) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 1; 
    timeout.tv_usec = 0;
    int select_result = 1;
    while (select_result > 0) {
        select_result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
        if (select_result == -1) {
            perror("select failed");
            return -1;
        } else if (select_result == 0) {
            return -1;
        }

        if (!FD_ISSET(sockfd, &read_fds)) 
            return -1;

        unsigned char buffer[IP_MAXPACKET];
        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);
        ssize_t packet_len = recvfrom(sockfd, buffer, IP_MAXPACKET, MSG_DONTWAIT, (struct sockaddr *)&sender, &sender_len);

        if (packet_len < 0) {
            fprintf(stderr, "recvfrom error: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        if (inet_ntop(AF_INET, &(sender.sin_addr), responseIpAddr, INET_ADDRSTRLEN) == NULL)
            perror("Inet_ntop failed\n");
        struct iphdr *ip_header = (struct iphdr *)buffer;
        u_int8_t *icmp_packet = buffer + 4 * ip_header->ihl;
        struct icmphdr *icmp_header = (struct icmphdr *)icmp_packet;
        *response_type = icmp_header->type;
        if (*response_type == ICMP_TIME_EXCEEDED) {
            ip_header = (void *)icmp_header + 8;
			icmp_header = (void *)ip_header + 4 * ip_header->ihl;
        }
        int packetSeqNum = icmp_header->un.echo.sequence;
        if (icmp_header->un.echo.id == (getpid() & 0xFFFF) && packetSeqNum == seqNum) {
            struct timespec end;
            clock_gettime(CLOCK_MONOTONIC, &end);

            double elapsed_ms = (end.tv_sec - start->tv_sec) * 1000.0 + (end.tv_nsec - start->tv_nsec) / 1000000.0;
            return elapsed_ms;
        }
    }
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Wrong number of arguments, expected one IP address.\nDon't forget sudo\n");
        return 1;
    }
    if (!isValidIPAddress(argv[1])) {
        printf("Invalid IP address.\n");
        return 1;
    }

    // Stworzenie gniazda
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }
    int is_reached = false;
    for (int ttl = 1; ttl <= 30 && !is_reached; ttl++) {
        char responseIpAddr[3][INET_ADDRSTRLEN];
        double elapsed_time[3];
        printf("%d. ", ttl);
        bool are_all_on_time = true;
        bool any_response = false;
        for (int i = 0; i < 3; i++) {
            int response_type;
            struct timespec start;
            elapsed_time[i] = sendPacket(sockfd, ttl * 3 + i, ttl, argv[1], &start);
            elapsed_time[i] = receivePacket(sockfd, responseIpAddr[i], &response_type, &start, ttl * 3 + i);
            if (elapsed_time[i] != -1 && response_type == ICMP_ECHOREPLY)
                is_reached = true;
            bool is_new = true;
            for (int j = 0; j < i; j++) {
                bool are_the_same = true;
                for (int k = 0; k < INET_ADDRSTRLEN; k++)
                    are_the_same &= responseIpAddr[i][k] == responseIpAddr[j][k];
                is_new &= !are_the_same;
            }
            if (is_new && elapsed_time[i] != -1)
                printf("%s ", responseIpAddr[i]);
            are_all_on_time &= elapsed_time[i] != -1;
            any_response |= elapsed_time[i] != -1;
        }
        if (!any_response)
            printf("*\n");
        else {
            if (are_all_on_time)
                printf("%dms\n", round_average(elapsed_time, 3));
            else
                printf("???\n");
        }
    }

    close(sockfd);

    return 0;
}
