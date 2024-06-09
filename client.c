#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1000
#define DATA_SIZE 400000
#define MAX_DATA 100000

uint32_t bitcnt[16];

void clear_histogram() {
    memset(bitcnt, 0, sizeof(bitcnt));
}

void load_histogram(uint32_t value) {
    uint32_t mask = 1;
    for (int i = 0; i < 16; i++) {
        if (value & mask) bitcnt[i]++;
        mask = mask * 2;
    }
}

void calculate_histogram(uint32_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        load_histogram(data[i]);
    }
}

void send_protocol(int sock, const char *name, char cmd, const char *param) {
    char sdata[BUFFER_SIZE];
    snprintf(sdata, sizeof(sdata), "@%s!%c:%s#", name, cmd, param);
    send(sock, sdata, strlen(sdata), MSG_NOSIGNAL | MSG_DONTWAIT);
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <name> <server_addr> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *name = argv[1];
    char *server_addr = argv[2];
    int port = atoi(argv[3]);

    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_addr, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d\n", server_addr, port);

    send_protocol(sock, name, 'N', "0");

    while (1) {
        int valread = read(sock, buffer, BUFFER_SIZE);
        if (valread <= 0) {
            perror("Read error");
            close(sock);
            exit(EXIT_FAILURE);
        }
        buffer[valread] = '\0';
        printf("Received from server: %s\n", buffer);

        if (buffer[11] == 'P') {
            int dataport;
            sscanf(buffer + 13, "%d", &dataport);

            int data_sock;
            struct sockaddr_in data_addr;
            if ((data_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("Data socket creation error");
                exit(EXIT_FAILURE);
            }

            data_addr.sin_family = AF_INET;
            data_addr.sin_port = htons(dataport);
            if (inet_pton(AF_INET, server_addr, &data_addr.sin_addr) <= 0) {
                perror("Invalid data address or address not supported");
                close(data_sock);
                exit(EXIT_FAILURE);
            }

            if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
                perror("Data connection failed");
                close(data_sock);
                exit(EXIT_FAILURE);
            }

            uint32_t data[MAX_DATA];
            size_t received = 0;
            while (received < DATA_SIZE) {
                int bytes = read(data_sock, (char *)data + received, DATA_SIZE - received);
                if (bytes <= 0) {
                    perror("Data read error");
                    close(data_sock);
                    exit(EXIT_FAILURE);
                }
                received += bytes;
            }
            close(data_sock);

            clear_histogram();
            calculate_histogram(data, MAX_DATA);

            char result[BUFFER_SIZE];
            snprintf(result, sizeof(result), "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", bitcnt[0], bitcnt[1], bitcnt[2], bitcnt[3], bitcnt[4], bitcnt[5], bitcnt[6], bitcnt[7], bitcnt[8], bitcnt[9], bitcnt[10], bitcnt[11], bitcnt[12], bitcnt[13], bitcnt[14], bitcnt[15]);
            send_protocol(sock, name, 'R', result);
        } else if (buffer[11] == 'X') {
            printf("No new data. Exiting...\n");
            break;
        } else if (buffer[11] == 'D') {
            printf("Result accepted. Exiting...\n");
            break;
        }
    }

    close(sock);
    return 0;
}
