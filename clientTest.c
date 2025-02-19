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
    // Erstellt einen IPv6-Datagram-Socket
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;

    // Aktiviert die Wiederverwendung der Adresse
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(sock);
        exit(EXIT_FAILURE);
    }

    #ifdef SO_REUSEPORT
    // Aktiviert die Wiederverwendung des Ports (falls verfügbar)
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
        perror("setsockopt(SO_REUSEPORT)");
        close(sock);
        exit(EXIT_FAILURE);
    }
    #endif

    // Zieladresse vorbereiten
    memset(dest_addr, 0, sizeof(*dest_addr));
    dest_addr->sin6_family = AF_INET6;               // IPv6-Protokollfamilie
    dest_addr->sin6_port = htons(port);              // Zielport

    // Umwandlung der Multicast-Adresse in binäres Format
    if (inet_pton(AF_INET6, multicast_addr, &dest_addr->sin6_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;  // Gibt den erstellten Socket zurück
}

// Funktion zum Senden einer Kontrollnachricht
void sendControlMessage(int sock, struct sockaddr_in6 *dest_addr, const char *message) {
    if (sendto(sock, message, strlen(message), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) < 0) {
        perror("sendto (control)");
    } else {
        printf("Sent control message: %s\n", message);
    }
}

// Funktion zum Verbindungsaufbau
void establishConnection(int sock, struct sockaddr_in6 *dest_addr) {
    sendControlMessage(sock, dest_addr, "HELLO");
    printf("Waiting for HELLO ACK...\n");

    char buffer[BUF_SIZE];
    struct sockaddr_in6 src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "HELLO ACK") == 0) {
            printf("Connection established.\n");
        } else {
            printf("Unexpected message: %s\n", buffer);
            exit(EXIT_FAILURE);
        }
    } else {
        perror("recvfrom (HELLO ACK)");
        exit(EXIT_FAILURE);
    }
}

// Funktion zum Verbindungsabbau
void terminateConnection(int sock, struct sockaddr_in6 *dest_addr) {
    sendControlMessage(sock, dest_addr, "CLOSE");
    printf("Waiting for CLOSE ACK...\n");

    char buffer[BUF_SIZE];
    struct sockaddr_in6 src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "CLOSE ACK") == 0) {
            printf("Connection terminated.\n");
        } else {
            printf("Unexpected message: %s\n", buffer);
        }
    } else {
        perror("recvfrom (CLOSE ACK)");
    }
}

// Funktion zum Senden eines Pakets über UDPv6 (SR-Protokollschicht)
void sendPacket(int sock, struct sockaddr_in6 *dest_addr, int seq_num, const char *data, float error_rate) {
    char packet[BUF_SIZE];  // Puffer für das Paket

    // Erstellen des Paketinhalts: Sequenznummer + Daten
    snprintf(packet, sizeof(packet), "%d:%s", seq_num, data);

    // Speichert das gesendete Paket im Puffer
    strncpy(sent_packets[seq_num], packet, BUF_SIZE);
    packet_lengths[seq_num] = strlen(packet);

    // Zufällige Zahl zur Simulation eines Fehlers generieren
    float random_value = (float)rand() / RAND_MAX;

    // Wenn der zufällige Wert kleiner als die Fehlerquote ist, überspringe das Senden
    if (random_value < error_rate) {
        printf("Packet %d dropped due to simulated error (error rate: %.2f)\n", seq_num, error_rate);
        return;
    }

    // Senden des Pakets an die Zieladresse
    if (sendto(sock, packet, strlen(packet), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) < 0) {
        perror("sendto");
    }
    printf("Sent packet %d: %s", seq_num, data);  // Ausgabe der gesendeten Sequenznummer und Daten
}

// Verwaltung von Timern und Ereignissen (SR-Protokollschicht)
void manageTimersAndEvents(int sock, FILE *file, struct sockaddr_in6 *dest_addr, float error_rate) {
    fd_set readfds;                      // Datei-Deskriptoren-Menge für select()
    struct timeval interval = {0, DEFAULT_INTERVAL};  // Zeitintervall für das Senden
    char buffer[BUF_SIZE];               // Puffer für das Lesen von Zeilen aus der Datei
    int seq_num = 0;                     // Aktuelle Sequenznummer des Pakets
    int timeout_count = 0;               // Zählt, wie oft das Timeout erreicht wurde

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        int activity = select(sock + 1, &readfds, NULL, NULL, &interval);

        if (activity < 0) {
            perror("select");
            break;
        }

        if (activity == 0) { // Timer abgelaufen
            timeout_count++;
            if (timeout_count >= 3) { // Nach 3 Intervallen weiter
                printf("Timeout: Moving to next packet...\n");
                timeout_count = 0;

                if (readFileLine(file, buffer, BUF_SIZE)) {
                    sendPacket(sock, dest_addr, seq_num, buffer, error_rate);
                    seq_num++;
                } else {
                    printf("End of file reached.\n");
                    break;
                }
            }
        } else if (FD_ISSET(sock, &readfds)) { // Datenempfang
            char recv_buffer[BUF_SIZE];
            struct sockaddr_in6 src_addr;
            socklen_t src_addr_len = sizeof(src_addr);

            ssize_t len = recvfrom(sock, recv_buffer, sizeof(recv_buffer) - 1, 0,
                                   (struct sockaddr *)&src_addr, &src_addr_len);
            if (len > 0) {
                recv_buffer[len] = '\0';
                if (strncmp(recv_buffer, "NACK:", 5) == 0) {
                    int nack_seq = atoi(recv_buffer + 5);
                    printf("Received NACK for packet %d. Resending...\n", nack_seq);

                    if (nack_seq >= 0 && nack_seq < MAX_SEQ_NUM && packet_lengths[nack_seq] > 0) {
                        sendto(sock, sent_packets[nack_seq], packet_lengths[nack_seq], 0,
                               (struct sockaddr *)dest_addr, sizeof(*dest_addr));
                        timeout_count = 0; // Timeout-Zähler zurücksetzen
                    }
                }
            }
        }

        // Intervall zurücksetzen
        interval.tv_sec = 0;
        interval.tv_usec = DEFAULT_INTERVAL;
    }
}

int main(int argc, char *argv[]) {
    // Überprüfung der Argumentanzahl
    if (argc != 6) {
        printf("Usage: client <file> <multicast_addr> <port> <window_size> <error_rate>\n");
        exit(EXIT_FAILURE);
    }

    // Argumente einlesen
    char *filename = argv[1];           // Name der zu sendenden Datei
    char *multicast_addr = argv[2];     // IPv6-Multicast-Adresse
    int port = atoi(argv[3]);           // Zielport
    int window_size = atoi(argv[4]);    // Fenstergröße (1 bis MAX_WINDOW_SIZE)
    float error_rate = atof(argv[5]);   // Fehlerquote

    // Überprüfung der Fenstergröße
    if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
        fprintf(stderr, "Window size must be between 1 and %d %d.\n", MAX_WINDOW_SIZE, window_size);
        exit(EXIT_FAILURE);
    }

    // Überprüfung der Fehlerquote
    if (error_rate < 0.0 || error_rate > 1.0) {
        fprintf(stderr, "Error rate must be between 0.0 and 1.0.\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in6 dest_addr;  // Zieladresse für Multicast

    // Initialisiert den Socket für den Multicast-Versand
    int sock = initializeSenderSocket(multicast_addr, port, &dest_addr);

    // Öffnet die Datei im Lese-Modus
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("fopen");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Verbindungsaufbau
    establishConnection(sock, &dest_addr);

    // Verwaltung von Timern und Ereignissen
    manageTimersAndEvents(sock, file, &dest_addr, error_rate);

    // Verbindungsabbau
    terminateConnection(sock, &dest_addr);

    fclose(file);  // Schließt die Datei
    close(sock);   // Schließt den Socket
    return 0;      // Beendet das Programm
}
