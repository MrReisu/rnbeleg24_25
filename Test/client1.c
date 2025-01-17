/* ToDo 
Fehler einbauen
-> Randomizer, wo zufällige Zeile nicht übertragen wird ggf. einbauen
Anzeige wie viele Zeilen übertragen wurden und Fehlerquote angeben, als separaten code damit man es aktivieren und deaktivieren kann

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define BUF_SIZE 1024             // Maximale Größe eines Datenpakets
#define DEFAULT_INTERVAL 3000000   // Zeitintervall für das Senden von Paketen in Mikrosekunden (300 ms)
#define MAX_WINDOW_SIZE 10        // Maximale Fenstergröße

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

// Funktion zum Senden eines Pakets über UDPv6 (SR-Protokollschicht)
void sendPacket(int sock, struct sockaddr_in6 *dest_addr, int seq_num, const char *data) {
    char packet[BUF_SIZE];  // Puffer für das Paket

    // Erstellen des Paketinhalts: Sequenznummer + Daten
    snprintf(packet, sizeof(packet), "%d:%s", seq_num, data);

    // Senden des Pakets an die Zieladresse
    if (sendto(sock, packet, strlen(packet), 0, (struct sockaddr *)dest_addr, sizeof(*dest_addr)) < 0) {
        perror("sendto");
    }
    printf("Sent packet %d: %s", seq_num, data);  // Ausgabe der gesendeten Sequenznummer und Daten
}

// Verwaltung von Timern und Ereignissen (SR-Protokollschicht)
void manageTimersAndEvents(int sock, FILE *file, struct sockaddr_in6 *dest_addr) {
    fd_set readfds;                      // Datei-Deskriptoren-Menge für select()
    struct timeval interval = {0, DEFAULT_INTERVAL};  // Zeitintervall für das Senden
    char buffer[BUF_SIZE];               // Puffer für das Lesen von Zeilen aus der Datei
    int seq_num = 0;                     // Aktuelle Sequenznummer des Pakets

    while (1) {
        FD_ZERO(&readfds);               // Leert die Datei-Deskriptoren-Menge
        FD_SET(sock, &readfds);          // Fügt den Socket zur Überwachung hinzu

        // Warten auf ein Ereignis (Timeout oder eingehende Nachricht)
        int activity = select(sock + 1, &readfds, NULL, NULL, &interval);

        if (activity < 0) {
            perror("select");
            break;
        }

        if (activity == 0) {
            // Timer abgelaufen: neues Paket senden
            if (readFileLine(file, buffer, BUF_SIZE)) {
                sendPacket(sock, dest_addr, seq_num, buffer);
                seq_num++;  // Erhöht die Sequenznummer für das nächste Paket
            } else {
                printf("\nEnd of file reached.\n");
                break;  // Beendet die Schleife, wenn das Dateiende erreicht ist
            }
        }

        if (FD_ISSET(sock, &readfds)) {
            // Eingehende Nachricht (z. B. NACK) verarbeiten
            char recv_buffer[BUF_SIZE];
            struct sockaddr_in6 src_addr;
            socklen_t src_addr_len = sizeof(src_addr);

            // Empfang eines Pakets
            ssize_t len = recvfrom(sock, recv_buffer, sizeof(recv_buffer) - 1, 0,
                                   (struct sockaddr *)&src_addr, &src_addr_len);
            if (len > 0) {
                recv_buffer[len] = '\0';  // Null-Terminierung für die Verarbeitung als String
                printf("Received message: %s\n", recv_buffer);  // Ausgabe der empfangenen Nachricht
            }
        }

        // Setzt das Intervall für das nächste Ereignis zurück
        interval.tv_sec = 0;
        interval.tv_usec = DEFAULT_INTERVAL;
    }
}

int main(int argc, char *argv[]) {
    // Überprüfung der Argumentanzahl
    if (argc != 5) {
        printf("Usage: client <file> <multicast_addr> <port> <window_size>\n");
        exit(EXIT_FAILURE);
    }

    // Argumente einlesen
    char *filename = argv[1];           // Name der zu sendenden Datei
    char *multicast_addr = argv[2];     // IPv6-Multicast-Adresse
    int port = atoi(argv[3]);           // Zielport
    int window_size = atoi(argv[4]);    // Fenstergröße (1 bis MAX_WINDOW_SIZE)

    // Überprüfung der Fenstergröße
    if (window_size < 1 || window_size > MAX_WINDOW_SIZE) {
        fprintf(stderr, "Window size must be between 1 and %d.\n", MAX_WINDOW_SIZE);
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

    // Verwaltung von Timern und Ereignissen
    manageTimersAndEvents(sock, file, &dest_addr);

    fclose(file);  // Schließt die Datei
    close(sock);   // Schließt den Socket
    return 0;      // Beendet das Programm
}
