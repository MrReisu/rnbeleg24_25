/* server.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>

#define BUF_SIZE 1024  // Maximale Größe eines empfangenen Pakets

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

    // Beitritt zur Multicast-Gruppe
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

    // Endlosschleife für den Empfang von Multicast-Nachrichten
    while (1) {
        struct sockaddr_in6 src_addr;  // Absenderadresse
        socklen_t src_addr_len = sizeof(src_addr);

        // Empfang eines Pakets
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&src_addr, &src_addr_len);
        if (len < 0) {
            perror("recvfrom");
            break;
        }

        buffer[len] = '\0';  // Null-Terminierung des empfangenen Pakets

        // Nachricht mit Zeitstempel in die Datei schreiben
        logMessageToFile(output_file, buffer);

        // Optional: Nachricht auf der Konsole ausgeben
        printf("Received and logged: %s\n", buffer);
    }

    // Schließen des Sockets
    close(sock);
    return 0;
}
