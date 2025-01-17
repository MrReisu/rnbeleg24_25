/* server.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

#define BUF_SIZE 1024  // Maximale Größe eines empfangenen Pakets
#define MAX_SEQ_NUM 1000  // Maximale erwartete Sequenznummer

// Funktion zur Ausgabe der Nutzungsanleitung
void usage() {
    printf("Usage: server <multicast_addr> <port> <output_file>\n");
    exit(EXIT_FAILURE);
}

// Funktion zum Hinzufügen von Datum und Uhrzeit zur Datei
void logMessageToFile(const char *filename, const char *message) {
    FILE *file = fopen(filename, "a");  // Öffnet die Datei im Anhängemodus, erstellt sie falls nötig
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Holt das aktuelle Datum und die aktuelle Uhrzeit
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // Formatiert Datum und Uhrzeit
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // Nachricht mit Zeitstempel in die Datei schreiben
    fprintf(file, "%s - %s\n", time_str, message);

    fclose(file);  // Schließt die Datei
}

// Funktion zur Überprüfung der Sequenznummern und Generierung von NACKs
void handleSequenceNumber(int sock, struct sockaddr_in6 *src_addr, socklen_t src_addr_len, int expected_seq, int received_seq) {
    if (received_seq != expected_seq) {
        // Lücke erkannt, sende NACK
        char nack_msg[BUF_SIZE];
        snprintf(nack_msg, sizeof(nack_msg), "NACK:%d", expected_seq);

        if (sendto(sock, nack_msg, strlen(nack_msg), 0, (struct sockaddr *)src_addr, src_addr_len) < 0) {
            perror("sendto (NACK)");
        } else {
            printf("Sent NACK for missing packet: %d\n", expected_seq);
        }
    }
}

int main(int argc, char *argv[]) {
    // Überprüfung der Argumentanzahl
    if (argc != 4) {
        usage();
    }

    // Einlesen der Kommandozeilenargumente
    char *multicast_addr = argv[1];  // IPv6-Multicast-Adresse
    int port = atoi(argv[2]);        // Portnummer
    char *output_file = argv[3];     // Name der Ausgabedatei

    // Erstellen des Sockets für UDPv6
    printf("Trying to create socket.\n");
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    printf("Socket set up.\n");

    // Konfiguration der lokalen Adresse
    printf("Configure local adress.\n");
    struct sockaddr_in6 local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin6_family = AF_INET6;         // IPv6-Protokollfamilie
    local_addr.sin6_port = htons(port);        // Lokaler Port
    local_addr.sin6_addr = in6addr_any;        // Empfang von allen Schnittstellen

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

    // Binden des Sockets an die lokale Adresse
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Beitritt zur Multicast-Gruppe
    printf("Entering Multicast-Group.\n");
    struct ipv6_mreq mreq;  // Multicast-Optionen
    if (inet_pton(AF_INET6, multicast_addr, &mreq.ipv6mr_multiaddr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }
    mreq.ipv6mr_interface = 0;  // Standard-Netzwerkschnittstelle

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt");
        close(sock);
        exit(EXIT_FAILURE);
    }

    char buffer[BUF_SIZE];  // Puffer für eingehende Nachrichten
    int expected_seq = 0;   // Nächste erwartete Sequenznummer

    // Endlosschleife für den Empfang von Multicast-Nachrichten
    while (1) {
        printf("Listening...\n");
        struct sockaddr_in6 src_addr;  // Absenderadresse
        socklen_t src_addr_len = sizeof(src_addr);

        // Empfang eines Pakets
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
        if (len < 0) {
            perror("recvfrom");
            break;
        }

        buffer[len] = '\0';  // Null-Terminierung des empfangenen Pakets

        // Extrahieren der Sequenznummer
        int received_seq;
        char *payload = strchr(buffer, ':');
        if (payload) {
            *payload = '\0';  // Trennt die Sequenznummer vom Payload
            payload++;
            received_seq = atoi(buffer);
        } else {
            printf("Malformed packet: %s\n", buffer);
            continue;
        }

        // Überprüfen der Sequenznummer und Generierung von NACKs bei Bedarf
        handleSequenceNumber(sock, &src_addr, src_addr_len, expected_seq, received_seq);

        // Protokollieren der empfangenen Nachricht
        char log_msg[BUF_SIZE];
        snprintf(log_msg, sizeof(log_msg), "Seq %d: %s", received_seq, payload);
        logMessageToFile(output_file, log_msg);

        printf("Received and logged: %s\n", log_msg);

        // Aktualisieren der erwarteten Sequenznummer
        if (received_seq == expected_seq) {
            expected_seq++;
        } else {
            printf("Out of order packet: expected %d, got %d\n", expected_seq, received_seq);
        }
    }

    // Schließen des Sockets
    close(sock);
    return 0;
}
