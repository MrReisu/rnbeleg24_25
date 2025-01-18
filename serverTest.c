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

// Funktion zur Verarbeitung von Kontrollnachrichten
void handleControlMessage(const char *message, int sock, struct sockaddr_in6 *src_addr, socklen_t src_addr_len, int *expected_seq) {
    if (strcmp(message, "HELLO") == 0) {
        printf("Received HELLO. Sending HELLO ACK...\n");
        sendto(sock, "HELLO ACK", strlen("HELLO ACK"), 0, (struct sockaddr *)src_addr, src_addr_len);
        printf("HELLO ACK sent.\n");
    } else if (strcmp(message, "CLOSE") == 0) {
        printf("Received CLOSE. Sending CLOSE ACK...\n");
        sendto(sock, "CLOSE ACK", strlen("CLOSE ACK"), 0, (struct sockaddr *)src_addr, src_addr_len);
        printf("CLOSE ACK sent. Resetting expected sequence number to 0.\n");
        *expected_seq = 0;  // Setze die erwartete Sequenznummer zurück
    }
}


// Funktion zur Überprüfung der Sequenznummern und Generierung von NACKs
void handleSequenceNumber(int sock, struct sockaddr_in6 *src_addr, socklen_t src_addr_len, int expected_seq, int received_seq) {
    if (received_seq != expected_seq) {
        // Lücke erkannt, sende NACK
        printf("Sequence mismatch. Expected: %d, Received: %d. Sending NACK...\n", expected_seq, received_seq);
        char nack_msg[BUF_SIZE];
        snprintf(nack_msg, sizeof(nack_msg), "NACK:%d", expected_seq);

        if (sendto(sock, nack_msg, strlen(nack_msg), 0, (struct sockaddr *)src_addr, src_addr_len) < 0) {
            perror("sendto (NACK)");
        } else {
            printf("NACK for sequence %d sent.\n", expected_seq);
        }
    } else {
        printf("Sequence match. Expected: %d, Received: %d.\n", expected_seq, received_seq);
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
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    printf("Socket created. Binding to local address...\n");

    // Konfiguration der lokalen Adresse
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

    printf("Socket bound to port %d. Joining multicast group %s...\n", port, multicast_addr);

    // Beitritt zur Multicast-Gruppe
    struct ipv6_mreq mreq;  // Multicast-Optionen

    // Multicast-Adresse setzen
    if (inet_pton(AF_INET6, multicast_addr, &mreq.ipv6mr_multiaddr) <= 0) {
        perror("inet_pton");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Schnittstelle explizit setzen
    mreq.ipv6mr_interface = if_nametoindex("eth0");  // Ersetze "eth0" durch die tatsächliche Schnittstelle
    if (mreq.ipv6mr_interface == 0) {
        perror("if_nametoindex");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Multicast-Beitritt
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Joined multicast group %s. Waiting for messages...\n", multicast_addr);

    char buffer[BUF_SIZE];  // Puffer für eingehende Nachrichten
    int expected_seq = 0;   // Nächste erwartete Sequenznummer
    fd_set readfds;  // Datei-Deskriptoren-Menge für select()
    struct timeval timeout;  // Timeout für select()


    // Endlosschleife für den Empfang von Multicast-Nachrichten
    while (1) {
        FD_ZERO(&readfds);  // Löscht die Deskriptoren-Menge
        FD_SET(sock, &readfds);  // Fügt den Server-Socket zur Überwachung hinzu

        timeout.tv_sec = 5;  // Timeout von 5 Sekunden
        timeout.tv_usec = 0;

        printf("Waiting for incoming messages...\n");

        int activity = select(sock + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select");
            break;
        }

        if (activity == 0) {
            printf("\n Timeout: No messages received within 5 seconds.\n");
            continue;  // Zurück zum Anfang der Schleife
        }

        if (FD_ISSET(sock, &readfds)) {
            struct sockaddr_in6 src_addr;  // Absenderadresse
            socklen_t src_addr_len = sizeof(src_addr);
        
            // Empfang eines Pakets
            ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
            if (len < 0) {
                perror("recvfrom");
                break;
            }

            buffer[len] = '\0';  // Null-Terminierung des empfangenen Pakets
            printf("Received message: %s\n", buffer);

            // Prüfen auf Kontrollnachrichten
            if (strcmp(buffer, "HELLO") == 0 || strcmp(buffer, "CLOSE") == 0) {
                handleControlMessage(buffer, sock, &src_addr, src_addr_len, &expected_seq);
                if (strcmp(buffer, "CLOSE") == 0) {
                    printf("Reset expected sequence number to 0 after CLOSE ACK.\n");
                }
                continue;
            }


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

            // Aktualisieren der erwarteten Sequenznummer
            if (received_seq == expected_seq) {
                expected_seq++;
            } else {
                printf("Out of order packet: expected %d, got %d\n", expected_seq, received_seq);
            }
    }
    }

    printf("Shutting down server...\n");

    // Schließen des Sockets
    close(sock);
    return 0;
    }

