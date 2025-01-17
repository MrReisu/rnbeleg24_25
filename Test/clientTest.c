#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <fcntl.h>

#define MAX_LINE 1024 // Maximale Länge einer Nachricht/Zeile
#define INTERVAL 300000 // Intervallzeit für das Senden (300 ms in Mikrosekunden)
#define TIMEOUT 900000 // Timeout-Wert (3 * 300 ms)
#define MAX_WINDOW_SIZE 10 // Maximale Größe des Sendefensters
#define PORT 50000 // Fester Port für Client-Server-Kommunikation

// Funktion zur Anzeige der korrekten Nutzung des Programms
void usage(const char *progname) {
    fprintf(stderr, "Usage: %s <server_address> <filename> <window_size>\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        usage(argv[0]);
    }

    const char *server_address = argv[1]; // Server-Adresse
    const char *filename = argv[2]; // Zu sendende Datei
    int window_size = atoi(argv[3]); // Fenstergröße

    if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
        fprintf(stderr, "Window size must be between 1 and %d\n", MAX_WINDOW_SIZE);
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen(filename, "r"); // Öffnen der Datei zur Übertragung
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0); // Erstellen eines UDP-Sockets
    if (sock < 0) {
        perror("socket");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET; // IPv4-Protokoll
    server_addr.sin_port = htons(PORT); // Fester Port 50000 für die Kommunikation
    // Der Port ist hardcodiert und wird sowohl vom Client als auch vom Server verwendet.
    if (inet_pton(AF_INET, server_address, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        fclose(file);
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_LINE]; // Puffer für Zeilen aus der Datei
    int seq_num = 0; // Sequenznummer für Pakete
    int base = 0; // Basis des Sendefensters
    int next_seq_num = 0; // Nächste Sequenznummer

    int acked[MAX_WINDOW_SIZE] = {0}; // Status der ACKs für Pakete
    char window[MAX_WINDOW_SIZE][MAX_LINE]; // Puffer für Pakete im Fenster

    struct timeval interval = {0, INTERVAL}; // Intervallzeit

    printf("Starting transmission...
");

    fd_set read_fds; // File-Descriptor-Set für select()

    // Dateiübertragung mit Fenstergröße
    while (!feof(file) || base < next_seq_num) {
        // Neue Pakete im Fenster senden
        while (next_seq_num < base + window_size && fgets(buffer, MAX_LINE, file)) {
            snprintf(window[next_seq_num % window_size], MAX_LINE, "%d:%s", next_seq_num, buffer);
            sendto(sock, window[next_seq_num % window_size], strlen(window[next_seq_num % window_size]), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
            printf("Sent packet %d: %s", next_seq_num, buffer);
            next_seq_num++;
        }

        // Warten auf ACKs oder NACKs
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        struct timeval timeout = {0, INTERVAL};
        int ret = select(sock + 1, &read_fds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(sock, &read_fds)) {
            char ack_buffer[16];
            recvfrom(sock, ack_buffer, sizeof(ack_buffer) - 1, 0, NULL, NULL);
            ack_buffer[sizeof(ack_buffer) - 1] = '\0';

            int ack_num;
            if (sscanf(ack_buffer, "ACK %d", &ack_num) == 1) {
                printf("Received ACK for packet %d\n", ack_num);
                if (ack_num >= base && ack_num < base + window_size) {
                    acked[ack_num % window_size] = 1;
                }
            } else if (sscanf(ack_buffer, "NACK %d", &ack_num) == 1) {
                printf("Received NACK for packet %d\n", ack_num);
                if (ack_num >= base && ack_num < base + window_size) {
                    sendto(sock, window[ack_num % window_size], strlen(window[ack_num % window_size]), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
                    printf("Resent packet %d\n", ack_num);
                }
            }
        }

        // Fenster verschieben, wenn Pakete bestätigt sind
        while (acked[base % window_size]) {
            acked[base % window_size] = 0;
            base++;
        }
    }

    printf("File transmission complete.\n");
    fclose(file);
    close(sock);
    return 0;
}