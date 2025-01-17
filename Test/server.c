#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_LINE 1024 // Maximale Länge einer Nachricht/Zeile
#define MAX_WINDOW_SIZE 10 // Maximale Fenstergröße

// Funktion zur Anzeige der korrekten Nutzung des Programms
void usage(const char *progname) {
    fprintf(stderr, "Usage: %s <port>\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
    }

    int port = atoi(argv[1]); // Zielport

    int sock = socket(AF_INET, SOCK_DGRAM, 0); // Erstellen eines UDP-Sockets
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in recv_addr = {0};
    recv_addr.sin_family = AF_INET; // IPv4-Protokoll
    recv_addr.sin_port = htons(port); // Portnummer
    recv_addr.sin_addr.s_addr = INADDR_ANY; // Empfang von allen Adressen

    if (bind(sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_LINE]; // Zwischenspeicher für empfangene Daten
    int expected_seq_num = 0; // Erwartete Sequenznummer

    printf("Waiting for packets on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            perror("recvfrom");
            continue;
        }

        buffer[len] = '\0'; // Nullterminierung

        int seq_num;
        char *data = strchr(buffer, ':'); // Trennen der Sequenznummer und Nutzdaten
        if (!data) {
            fprintf(stderr, "Malformed packet: %s\n", buffer);
            continue;
        }

        *data++ = '\0'; // Sequenznummer abschließen
        seq_num = atoi(buffer);

        if (seq_num == expected_seq_num) {
            printf("Received packet %d: %s", seq_num, data);
            expected_seq_num++;

            char ack[16];
            snprintf(ack, sizeof(ack), "ACK %d", seq_num);
            sendto(sock, ack, strlen(ack), 0, (struct sockaddr *)&client_addr, addr_len);
            printf("Sent ACK for packet %d\n", seq_num);
        } else if (seq_num > expected_seq_num) {
            printf("Unexpected packet %d, expected %d\n", seq_num, expected_seq_num);

            char nack[16];
            snprintf(nack, sizeof(nack), "NACK %d", expected_seq_num);
            sendto(sock, nack, strlen(nack), 0, (struct sockaddr *)&client_addr, addr_len);
            printf("Sent NACK for packet %d\n", expected_seq_num);
        }
    }

    close(sock);
    return 0;
}
