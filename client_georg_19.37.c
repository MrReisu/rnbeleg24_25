// client

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUF_SIZE 1024           // Maximale Größe eines Pakets
#define DEFAULT_INTERVAL 300000 // Standard-Zeitintervall in Mikrosekunden

// Funktion zum Initialisieren des UDPv6-Sendersockets
int initializeSenderSocket(const char *multicast_addr, int port, struct sockaddr_in6 *dest_addr) {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(dest_addr, 0, sizeof(*dest_addr));
    dest_addr->sin6_family = AF_INET6;
    dest_addr->sin6_port = htons(port);
    if (inet_pton(AF_INET6, multicast_addr, &dest_addr->sin6_addr) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }
    return sock;
}

// Funktion zum Senden einer Steuerungsnachricht
void sendControlMessage(int sock, struct sockaddr_in6 *dest_addr, const char *message) {
    if (sendto(sock, message, strlen(message), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) < 0) {
        perror("sendto (Control Message)");
    } else {
        printf("Sent control message: %s\n", message);
    }
}

// Funktion zum Senden eines regulären Pakets
void sendPacket(int sock, struct sockaddr_in6 *dest_addr, int seq_num, const char *data) {
    char packet[BUF_SIZE];
    snprintf(packet, sizeof(packet), "%d:%s", seq_num, data);

    if (sendto(sock, packet, strlen(packet), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) < 0) {
        perror("sendto (Packet)");
    } else {
        printf("Sent packet %d: %s\n", seq_num, data);
    }
}

// Hauptfunktion
int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <multicast_addr> <port> <file>\n", argv[0]);
        return 1;
    }

    const char *multicast_addr = argv[1];
    int port = atoi(argv[2]);
    const char *file_name = argv[3];

    FILE *file = fopen(file_name, "r");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in6 dest_addr;
    int sock = initializeSenderSocket(multicast_addr, port, &dest_addr);

    char buffer[BUF_SIZE];
    int seq_num = 1; // Start der Sequenznummer

    // Sende die initiale HELLO-Nachricht
    sendControlMessage(sock, &dest_addr, "HELLO");

    while (1) {
        // Lies und sende eine Zeile aus der Datei, falls vorhanden
        if (fgets(buffer, sizeof(buffer), file)) {
            sendPacket(sock, &dest_addr, seq_num, buffer);
            seq_num++; // Sequenznummer inkrementieren
            usleep(DEFAULT_INTERVAL); // Wartezeit vor dem nächsten Paket
        } else {
            printf("End of file reached.\n");
            break; // Beenden der Schleife, wenn das Ende der Datei erreicht ist
        }
    }

    // Sende die abschließende CLOSE-Nachricht
    sendControlMessage(sock, &dest_addr, "CLOSE");

    fclose(file);
    close(sock);
    return 0;
}
