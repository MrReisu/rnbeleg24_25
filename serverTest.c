#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1024  // Maximale Größe eines Pakets

// Funktion zur Verarbeitung von Kontrollnachrichten
void handleControlMessage(const char *message, int sock, struct sockaddr_in6 *src_addr, socklen_t src_addr_len, int *expected_seq) {
    struct sockaddr_in6 multicast_addr;

    // Multicast-Adresse vorbereiten
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin6_family = AF_INET6;
    multicast_addr.sin6_port = htons(50000);
    inet_pton(AF_INET6, "ff02::1", &multicast_addr.sin6_addr);

    if (strcmp(message, "HELLO") == 0) {
        printf("Received HELLO. Sending HELLO ACK via Multicast...\n");

        if (sendto(sock, "HELLO ACK", strlen("HELLO ACK"), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
            perror("sendto (HELLO ACK)");
        } else {
            printf("HELLO ACK sent via Multicast.\n");
        }

        // Setze die erwartete Sequenznummer auf 1
        *expected_seq = 1;
        printf("Initialized expected sequence number to 1.\n");
    } else if (strcmp(message, "CLOSE") == 0) {
        printf("Received CLOSE. Sending CLOSE ACK via Multicast...\n");

        if (sendto(sock, "CLOSE ACK", strlen("CLOSE ACK"), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr)) < 0) {
            perror("sendto (CLOSE ACK)");
        } else {
            printf("CLOSE ACK sent via Multicast.\n");
        }

        // Setze die erwartete Sequenznummer zurück
        printf("Resetting expected sequence number to 0.\n");
        *expected_seq = 0;
    } else if (strcmp(message, "HELLO ACK") == 0 || strcmp(message, "CLOSE ACK") == 0) {
        printf("Received ACK message: %s. Ignoring.\n", message);
    } else {
        printf("Unknown control message: %s\n", message);
    }
}

// Funktion zur Verarbeitung von Datenpaketen
void handleDataPacket(const char *message, int *expected_seq) {
    int seq_num;
    char data[BUF_SIZE];

    // Versuche die Nachricht zu parsen: "<seq_num>:<data>"
    if (sscanf(message, "%d:%1023[^\n]", &seq_num, data) == 2) {
        // Prüfen, ob die Sequenznummer korrekt ist
        if (*expected_seq > 0 && seq_num == *expected_seq) {
            printf("Received expected packet %d: %s\n", seq_num, data);
            (*expected_seq)++;  // Erwarte das nächste Paket
        } else if (*expected_seq == 0) {
            printf("Ignoring data packet %d: No HELLO received to initialize sequence.\n", seq_num);
        } else {
            printf("Out-of-order packet received (expected %d, got %d): %s\n", *expected_seq, seq_num, data);
        }
    } else {
        printf("Malformed data packet: %s\n", message);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <multicast_addr> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *multicast_addr = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Multicast-Adresse binden
    struct sockaddr_in6 local_addr = {0};
    local_addr.sin6_family = AF_INET6;
    local_addr.sin6_port = htons(port);
    local_addr.sin6_addr = in6addr_any;

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Beitritt zur Multicast-Gruppe
    struct ipv6_mreq mreq;
    inet_pton(AF_INET6, multicast_addr, &mreq.ipv6mr_multiaddr);
    mreq.ipv6mr_interface = 0; // Standard-Schnittstelle

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt (IPV6_JOIN_GROUP)");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Listening for messages on %s:%d\n", multicast_addr, port);

    // Empfangsschleife
    char buffer[BUF_SIZE];
    struct sockaddr_in6 src_addr;
    socklen_t src_addr_len = sizeof(src_addr);
    int expected_seq = 0;

    while (1) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[len] = '\0';  // Null-terminierter String

        // Debug: Zeige die empfangene Nachricht an
        printf("======================================\n\n");
        printf("Received message: %s\n", buffer);

        // Prüfe Kontrollnachricht oder Datenpaket
        if (strcmp(buffer, "HELLO") == 0 || strcmp(buffer, "CLOSE") == 0 || strcmp(buffer, "HELLO ACK") == 0 || strcmp(buffer, "CLOSE ACK") == 0) {
            handleControlMessage(buffer, sock, &src_addr, src_addr_len, &expected_seq);
        } else {
            handleDataPacket(buffer, &expected_seq);
        }
    }

    close(sock);
    return 0;
}
