/* client.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define BUF_SIZE 1024             // Maximale Größe eines Datenpakets
#define DEFAULT_INTERVAL 300000   // Zeitintervall für das Senden von Paketen in Mikrosekunden (300 ms)
#define MAX_WINDOW_SIZE 10        // Maximale Fenstergröße
#define MAX_SEQ_NUM 1000          // Maximale Anzahl an Paketen

char sent_packets[MAX_SEQ_NUM][BUF_SIZE];  // Puffer für gesendete Pakete
int packet_lengths[MAX_SEQ_NUM];          // Längen der gesendeten Pakete

// Funktion zum Lesen einer Zeile aus der Datei (Anwendungsschicht)
int readFileLine(FILE *file, char *buffer, int buffer_size) {
    return fgets(buffer, buffer_size, file) != NULL;  // Liest eine Zeile und gibt 1 zurück, wenn erfolgreich
}

// Funktion zum Initialisieren des UDPv6-Sendersockets (SR-Protokollschicht)
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

// Funktion zum Senden eines Pakets
void sendPacket(int sock, struct sockaddr_in6 *dest_addr, int seq_num, const char *data) {
    char packet[BUF_SIZE];
    snprintf(packet, sizeof(packet), "%d:%s", seq_num, data);
    // Speichert das gesendete Paket im Puffer
    strncpy(sent_packets[seq_num], packet, BUF_SIZE);
    packet_lengths[seq_num] = strlen(packet);

    if (sendto(sock, packet, strlen(packet), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) < 0) {
        perror("sendto");
    }
    printf("Sent packet %d: %s\n", seq_num, data);  // Ausgabe der gesendeten Sequenznummer und Daten
}

// Funktion zum Senden von Paketen in zufälliger Reihenfolge
void sendPacketsInRandomOrder(int sock, struct sockaddr_in6 *dest_addr, const char *data) {
    int max = 10;
    int seq_nums[max];
    for (int i = 0; i < max; i++) {
        seq_nums[i] = i;  // Initialisieren der Sequenznummern
    }

    // Zufällige Reihenfolge der Sequenznummern generieren
    srand(time(NULL));  // Initialisieren des Zufallszahlengenerators
    for (int i = 0; i < max; i++) {
        int j = rand() % max;  // Zufälliger Index
        int temp = seq_nums[i];
        seq_nums[i] = seq_nums[j];
        seq_nums[j] = temp;
    }

    // Senden der Pakete in der zufälligen Reihenfolge
    for (int i = 0; i < max; i++) {
        int seq_num = seq_nums[i];
        sendPacket(sock, dest_addr, seq_num, data);  // Die ursprüngliche Funktion zum Senden eines Pakets aufrufen
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

    while (readFileLine(file, buffer, sizeof(buffer))) {
        sendPacketsInRandomOrder(sock, &dest_addr, buffer);  // Senden der Pakete in zufälliger Reihenfolge
        usleep(DEFAULT_INTERVAL);  // Warten, bevor das nächste Paket gesendet wird
    }

    fclose(file);
    close(sock);
    return 0;
}