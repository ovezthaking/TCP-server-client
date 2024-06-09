// Klient
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#define NAME_SIZE 8
#define BUFFER_SIZE 1024
#define MAX_DATA 100000

uint32_t data[MAX_DATA];
uint32_t bitcnt[16];

void clear_histogram() {
    memset(bitcnt, 0, sizeof(bitcnt));
}

void calculate_histogram(uint32_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        for (int bit = 0; bit < 16; bit++) {
            if (data[i] & (1 << bit)) {
                bitcnt[bit]++;
            }
        }
    }
}

void send_protocol(int sock, const char *name, char command, const char *params) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "@%s0!%c:%s#", name, command, params);
    send(sock, buffer, strlen(buffer), MSG_NOSIGNAL | MSG_DONTWAIT);
    printf("Wysłano komendę: @%s0!%c:%s#\n", name, command, params); // Debug
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <name> <server_ip> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *name = argv[1];
    const char *server_ip = argv[2];
    int server_port = atoi(argv[3]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    printf("Łączenie z serwerem na adresie %s i porcie %d\n", server_ip, server_port);
    connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr));
    printf("Połączono z serwerem\n");

    send_protocol(sock, name, 'N', "0");
    printf("Wysłano żądanie nowych danych\n");

    char buffer[BUFFER_SIZE];
    int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Odebrano odpowiedź: %c %s\n", buffer[11], buffer + 13);

        if (buffer[11] == 'P') {
            int dataport = atoi(buffer + 13);
            int data_sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in data_addr;
            data_addr.sin_family = AF_INET;
            data_addr.sin_port = htons(dataport);
            inet_pton(AF_INET, server_ip, &data_addr.sin_addr);

            printf("Łączenie na port danych %d\n", dataport);
            connect(data_sock, (struct sockaddr *) &data_addr, sizeof(data_addr));
            printf("Odbieranie danych...\n");

            n = recv(data_sock, data, sizeof(data), 0);
            if (n == sizeof(data)) {
                printf("Odebrano wartości\n");
                clear_histogram();
                calculate_histogram(data, MAX_DATA);

                char result[BUFFER_SIZE];
                snprintf(result, sizeof(result), "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                         bitcnt[0], bitcnt[1], bitcnt[2], bitcnt[3], bitcnt[4], bitcnt[5], bitcnt[6], bitcnt[7],
                         bitcnt[8], bitcnt[9], bitcnt[10], bitcnt[11], bitcnt[12], bitcnt[13], bitcnt[14], bitcnt[15]);
                send_protocol(sock, name, 'R', result);
                printf("Wysłano wyniki do serwera\n");

                n = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    if (buffer[11] == 'D') {
                        printf("Odebrano potwierdzenie zakończenia połączenia\n");
                    }
                }
                close(data_sock);
            } else {
                printf("Błąd w odbieraniu danych\n");
            }
        }
    } else {
        printf("Błąd w odbieraniu odpowiedzi\n");
    }

    close(sock);
    printf("Zamykam połączenie\n");
    return 0;
}
