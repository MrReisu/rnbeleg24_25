#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_LINE 1024
#define INTERVAL 300000 // 300 ms in microseconds
#define MAX_WINDOW_SIZE 10

// Funktion zur Anzeige der korrekten Nutzung des Programms
void usage(const char *progname) {
    fprintf(stderr, "Usage: %s <multicast_address> <port>\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        usage(argv[0]);
    }

    const char *multicast_address = argv[1];
    int port = atoi(argv[2]);

    // Erstellen eines UDPv6-Sockets
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Konfiguration des Empfangsadresse
    struct sockaddr_in6 recv_addr = {0};
    recv_addr.sin6_family = AF_INET6;
    recv_addr.sin6_port = htons(port);
    recv_addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Beitritt zur Multicast-Gruppe
    struct ipv6_mreq mreq = {0};
    if (inet_pton(AF_INET6, multicast_address, &mreq.ipv6mr_multiaddr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }
    mreq.ipv6mr_interface = 0;

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_LINE];
    int expected_seq_num = 0;

    printf("Waiting for packets...\n");

    while (1) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[len] = '\0';
        if (strncmp(buffer, "HELLO", 5) == 0) {
            printf("Received HELLO\n");
            sendto(sock, "HELLO ACK", strlen("HELLO ACK"), 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr));
            printf("Sent HELLO ACK\n");
        } else if (strncmp(buffer, "CLOSE", 5) == 0) {
            printf("Received CLOSE\n");
            break;
        } else {
            int seq_num;
            char *data = strchr(buffer, ':');
            if (!data) {
                fprintf(stderr, "Malformed packet: %s\n", buffer);
                continue;
            }

            *data++ = '\0';
            seq_num = atoi(buffer);

            if (seq_num == expected_seq_num) {
                printf("Received packet %d: %s", seq_num, data);
                expected_seq_num++;

                char ack[16];
                snprintf(ack, sizeof(ack), "ACK %d", seq_num);
                sendto(sock, ack, strlen(ack), 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr));
                printf("Sent ACK for packet %d\n", seq_num);
            } else if (seq_num > expected_seq_num) {
                printf("Unexpected packet %d, expected %d\n", seq_num, expected_seq_num);

                char nack[16];
                snprintf(nack, sizeof(nack), "NACK %d", expected_seq_num);
                sendto(sock, nack, strlen(nack), 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr));
                printf("Sent NACK for packet %d\n", expected_seq_num);
            }
        }
    }

    close(sock);
    return 0;
}
