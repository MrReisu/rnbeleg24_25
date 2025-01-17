#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_LINE 1024 // Maximale Länge einer Nachricht/Zeile
#define MAX_WINDOW_SIZE 10 // Maximale Fenstergröße
#define PORT 50000 // Fester Port für Client-Server-Kommunikation

// Funktion zur Anzeige der korrekten Nutzung des Programms
void usage(const char *progname) {
    fprintf(stderr, "Usage: %s\n", progname);
    exit(EXIT_FAILURE);
}

int main() {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0); // Erstellen eines UDPv6-Sockets
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in6 recv_addr = {0};
    recv_addr.sin6_family = AF_INET6; // IPv6-Protokoll
    recv_addr.sin6_port = htons(PORT); // Fester Port 50000 für die Kommunikation
    recv_addr.sin6_addr = in6addr_any; // Empfang von allen IPv6-Adressen

    if (bind(sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_LINE]; // Zwischenspeicher für empfangene Daten
    printf("Waiting for packets on port %d...\n", PORT);

    struct sockaddr_in6 client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (1) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[len] = '\0'; // Nullterminierung

        if (strcmp(buffer, "HELLO") == 0) {
            printf("Received HELLO. Sending HELLO ACK.\n");
            sendto(sock, "HELLO ACK", strlen("HELLO ACK"), 0, (struct sockaddr *)&client_addr, addr_len);
        } else if (strcmp(buffer, "CLOSE") == 0) {
            printf("Received CLOSE. Sending CLOSE ACK.\n");
            sendto(sock, "CLOSE ACK", strlen("CLOSE ACK"), 0, (struct sockaddr *)&client_addr, addr_len);
            break;
        } else {
            // Standardpaketverarbeitung (ACK/NACK-Logik hier einfügen)
            printf("Received data: %s\n", buffer);
        }
    }

    close(sock);
    return 0;
}
