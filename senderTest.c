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

// Funktion zur Anzeige der korrekten Nutzung des Programms
void usage(const char *progname) {
    fprintf(stderr, "Usage: %s <filename> <multicast_address> <port> <window_size>\n", progname);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    // Überprüfen, ob die korrekten Argumente übergeben wurden
    if (argc != 5) {
        usage(argv[0]);
    }

    // Extrahieren der Argumente
    const char *filename = argv[1]; // Zu sendende Datei
    const char *multicast_address = argv[2]; // Multicast-Adresse
    int port = atoi(argv[3]); // Zielport
    int window_size = atoi(argv[4]); // Fenstergröße

    if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
        fprintf(stderr, "Window size must be between 1 and %d\n", MAX_WINDOW_SIZE);
        exit(EXIT_FAILURE);
    }

    // Öffnen der Datei, die gesendet werden soll
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Erstellen eines UDPv6-Sockets
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // Konfigurieren der Multicast-Adresse
    struct sockaddr_in6 multicast_addr = {0};
    multicast_addr.sin6_family = AF_INET6; // IPv6-Protokoll
    multicast_addr.sin6_port = htons(port); // Portnummer in Netzwerk-Byte-Reihenfolge
    if (inet_pton(AF_INET6, multicast_address, &multicast_addr.sin6_addr) <= 0) {
        perror("inet_pton");
        fclose(file);
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[MAX_LINE]; // Zwischenspeicher für Zeilen aus der Datei
    int seq_num = 0; // Sequenznummer für Pakete
    int base = 0; // Basis des Sendefensters (ältestes unbestätigtes Paket)
    int next_seq_num = 0; // Nächste Sequenznummer, die gesendet werden kann

    int acked[MAX_WINDOW_SIZE] = {0}; // Status der ACKs für Pakete im Fenster
    char window[MAX_WINDOW_SIZE][MAX_LINE]; // Puffer für Pakete im Sendefenster

    struct timeval interval = {0, INTERVAL}; // Intervall für das Senden von Paketen

    printf("Starting transmission...\n");

    // Verbindungsaufbau: Senden eines HELLO-Pakets
    sendto(sock, "HELLO", strlen("HELLO"), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
    printf("Sent HELLO packet\n");

    fd_set read_fds; // File-Descriptor-Set für select()
    while (1) {
        // Warten auf HELLO ACK
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        int ret = select(sock + 1, &read_fds, NULL, NULL, &interval);

        if (ret > 0 && FD_ISSET(sock, &read_fds)) {
            char ack_buffer[16]; // Zwischenspeicher für ACK/NACK-Nachrichten
            recvfrom(sock, ack_buffer, sizeof(ack_buffer) - 1, 0, NULL, NULL);
            ack_buffer[sizeof(ack_buffer) - 1] = '\0';

            if (strncmp(ack_buffer, "HELLO ACK", 9) == 0) {
                printf("Received HELLO ACK\n");
                break;
            }
        } else {
            printf("No HELLO ACK received, retrying...\n");
            sendto(sock, "HELLO", strlen("HELLO"), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
        }
    }

    printf("Connection established. Starting file transfer.\n");

    // Dateiübertragung mit Fenstergröße
    while (!feof(file) || base < next_seq_num) {
        // Senden neuer Pakete innerhalb des Fensters
        while (next_seq_num < base + window_size && fgets(buffer, MAX_LINE, file)) {
            snprintf(window[next_seq_num % window_size], MAX_LINE, "%d:%s", next_seq_num, buffer);
            sendto(sock, window[next_seq_num % window_size], strlen(window[next_seq_num % window_size]), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
            printf("Sent packet %d: %s", next_seq_num, buffer);
            next_seq_num++;
        }

        // Warten auf ACKs oder NACKs
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        struct timeval timeout = {0, INTERVAL};
        int ret = select(sock + 1, &read_fds, NULL, NULL, &timeout);

        if (ret > 0 && FD_ISSET(sock, &read_fds)) {
            char ack_buffer[16]; // Zwischenspeicher für ACK/NACK-Nachrichten
            recvfrom(sock, ack_buffer, sizeof(ack_buffer) - 1, 0, NULL, NULL);
            ack_buffer[sizeof(ack_buffer) - 1] = '\0';

            int ack_num; // Sequenznummer des bestätigten Pakets
            if (sscanf(ack_buffer, "ACK %d", &ack_num) == 1) {
                printf("Received ACK for packet %d\n", ack_num);
                if (ack_num >= base && ack_num < base + window_size) {
                    acked[ack_num % window_size] = 1; // Paket als bestätigt markieren
                }
            } else if (sscanf(ack_buffer, "NACK %d", &ack_num) == 1) {
                printf("Received NACK for packet %d\n", ack_num);
                if (ack_num >= base && ack_num < base + window_size) {
                    // Paket erneut senden
                    sendto(sock, window[ack_num % window_size], strlen(window[ack_num % window_size]), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
                    printf("Resent packet %d\n", ack_num);
                }
            }
        }

        // Prüfen, ob das Sendefenster verschoben werden kann
        while (acked[base % window_size]) {
            acked[base % window_size] = 0; // Fenster freigeben
            base++;
        }
    }

    // Verbindungsabbau: Senden eines CLOSE-Pakets
    sendto(sock, "CLOSE", strlen("CLOSE"), 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
    printf("Sent CLOSE packet\n");

    fclose(file);
    close(sock);
    return 0;
}