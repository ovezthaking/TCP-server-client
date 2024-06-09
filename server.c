// Serwer
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <fcntl.h>

#define NAME_SIZE 8
#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS 10
#define MAX_DATA 100000

struct CALCDATA {
    uint32_t data[MAX_DATA];
};

struct conninfo {
    int status;
    char name[NAME_SIZE + 1];
    int Csock;
    int protocol_state;
    int Asock;
    int Dsock;
    int dataport;
    struct CALCDATA data;
};

int lsock;
struct conninfo conn[MAX_CONNECTIONS];

void clear_histogram(uint32_t *histogram) {
    memset(histogram, 0, sizeof(uint32_t) * 16);
}

void load_histogram(uint32_t *histogram, uint32_t value) {
    uint32_t mask = 1;
    for (int i = 0; i < 16; i++) {
        if (value & mask) histogram[i]++;
        mask <<= 1;
    }
}

void create_data(struct CALCDATA *cdata) {
    if (cdata != NULL) {
        for (int i = 0; i < MAX_DATA; i++) {
            uint32_t v = (uint32_t) rand() ^ (uint32_t) rand();
            cdata->data[i] = v & 0x0000FFFF;
        }
    }
}

void send_protocol(int sock, const char *name, char command, const char *params) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "@%s0!%c:%s#", name, command, params);
    send(sock, buffer, strlen(buffer), MSG_NOSIGNAL | MSG_DONTWAIT);
    printf("Wysłano komendę: @%s0!%c:%s#\n", name, command, params); // Debug
}

void handle_client(int idx) {
    struct conninfo *c = &conn[idx];
    char buffer[BUFFER_SIZE];
    int n = recv(c->Csock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("Odebrano komendę: %c\n", buffer[11]);
        if (buffer[11] == 'N') {
            create_data(&c->data);
            snprintf(buffer, sizeof(buffer), "%d", c->dataport);
            send_protocol(c->Csock, "00000000", 'P', buffer);
            printf("Wysłano port danych: %d\n", c->dataport);
            c->protocol_state = 2;

            // Otwórz połączenie danych
            c->Asock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in data_addr;
            data_addr.sin_family = AF_INET;
            data_addr.sin_addr.s_addr = INADDR_ANY;
            data_addr.sin_port = htons(c->dataport);
            bind(c->Asock, (struct sockaddr *) &data_addr, sizeof(data_addr));
            listen(c->Asock, 1);
        }
    } else if (n == 0) {
        printf("Klient zamknął połączenie.\n");
        close(c->Csock);
        c->status = 0;
    } else {
        perror("recv");
    }
}

void handle_data_connection(int idx) {
    struct conninfo *c = &conn[idx];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    c->Dsock = accept(c->Asock, (struct sockaddr *) &client_addr, &addr_len);
    if (c->Dsock > 0) {
        printf("Połączenie danych na porcie %d od %s\n", c->dataport, inet_ntoa(client_addr.sin_addr));
        send(c->Dsock, c->data.data, sizeof(c->data.data), 0);
        printf("Wysłano dane do klienta\n");

        char buffer[BUFFER_SIZE];
        int n = recv(c->Dsock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            printf("Odebrano wyniki od klienta\n");
            uint32_t histogram[16];
            clear_histogram(histogram);
            sscanf(buffer + 13, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                   &histogram[0], &histogram[1], &histogram[2], &histogram[3],
                   &histogram[4], &histogram[5], &histogram[6], &histogram[7],
                   &histogram[8], &histogram[9], &histogram[10], &histogram[11],
                   &histogram[12], &histogram[13], &histogram[14], &histogram[15]);
            printf("Wyniki poprawne, zamykam połączenie\n");
            send_protocol(c->Csock, "00000000", 'D', "0");
            close(c->Dsock);
            close(c->Asock);
            c->status = 0;
        } else {
            perror("recv");
        }
    } else {
        perror("accept");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    lsock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    bind(lsock, (struct sockaddr *) &server_addr, sizeof(server_addr));
    listen(lsock, 10);
    printf("Serwer uruchomiony na porcie %d. Oczekiwanie na połączenia...\n", port);

    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(lsock, &read_fds);
        int max_sd = lsock;

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (conn[i].status == 1) {
                FD_SET(conn[i].Csock, &read_fds);
                if (conn[i].Csock > max_sd) max_sd = conn[i].Csock;
            }
            if (conn[i].protocol_state == 2) {
                FD_SET(conn[i].Asock, &read_fds);
                if (conn[i].Asock > max_sd) max_sd = conn[i].Asock;
            }
        }

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        if (FD_ISSET(lsock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_socket = accept(lsock, (struct sockaddr *) &client_addr, &addr_len);
            if (new_socket >= 0) {
                for (int i = 0; i < MAX_CONNECTIONS; i++) {
                    if (conn[i].status == 0) {
                        conn[i].status = 1;
                        conn[i].Csock = new_socket;
                        conn[i].protocol_state = 1;
                        conn[i].dataport = port + i + 1;
                        printf("Nowe połączenie od %s na porcie %d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                        break;
                    }
                }
            } else {
                perror("accept");
            }
        }

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (conn[i].status == 1 && FD_ISSET(conn[i].Csock, &read_fds)) {
                handle_client(i);
            }
            if (conn[i].protocol_state == 2 && FD_ISSET(conn[i].Asock, &read_fds)) {
                handle_data_connection(i);
            }
        }
    }

    return 0;
}
