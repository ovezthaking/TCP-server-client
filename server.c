#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>

#define MAX_CONNECTION 100
#define NAME_SIZE 8
#define MAX_DATA 100000
#define DATA_PORT_BASE 7000
#define BUFFER_SIZE 1000

struct CALCDATA {
    uint32_t data[MAX_DATA];
};

struct conninfo {
    int status;
    char name[NAME_SIZE+1];
    int Csock;
    int protocol_state;
    int Asock;
    int Dsock;
    int dataport;
    struct CALCDATA data;
};

int lsock;
struct conninfo conn[MAX_CONNECTION];
uint32_t histogram[16];

void clear_histogram() {
    memset(histogram, 0, sizeof(histogram));
}

void load_histogram(uint32_t value) {
    uint32_t mask = 1;
    for (int i = 0; i < 16; i++) {
        if (value & mask) histogram[i]++;
        mask = mask * 2;
    }
}

int create_data(int idx, struct CALCDATA *cdata) {
    if (cdata != NULL) {
        uint32_t i;
        uint32_t *value;
        uint32_t v;
        value = &cdata->data[0];
        for (i = 0; i < MAX_DATA; i++) {
            v = (uint32_t) rand() ^ (uint32_t) rand();
            printf("Creating value #%d v=%u addr=%lu \r", i, v, (unsigned long)value);
            *value = v & 0x0000FFFF;
            value++;
        }
        return 1;
    }
    return 0;
}

void send_protocol(int idx, char cmd, const char* param) {
    char sdata[BUFFER_SIZE];
    snprintf(sdata, sizeof(sdata), "@000000000!%c:%s#", cmd, param);
    send(conn[idx].Csock, sdata, strlen(sdata), MSG_NOSIGNAL | MSG_DONTWAIT);
}

void handle_client_command(int idx, char* command) {
    if (command[0] == 'N') {
        conn[idx].dataport = DATA_PORT_BASE + idx + 1;
        int create_result = create_data(idx, &conn[idx].data);
        if (create_result != 0) {
            char sdata[BUFFER_SIZE];
            sprintf(sdata, "%d", conn[idx].dataport);
            send_protocol(idx, 'P', sdata);
            conn[idx].protocol_state = 2;
        } else {
            send_protocol(idx, 'X', "0");
        }
    } else if (command[0] == 'R') {
        // process result (to be implemented)
    } else if (command[0] == 'E') {
        // handle error (to be implemented)
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    fd_set read_fds;
    int max_sd, activity, new_socket;
    char buffer[BUFFER_SIZE];

    // Initialize listening socket
    lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, '0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(lsock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(lsock);
        exit(EXIT_FAILURE);
    }

    if (listen(lsock, 10) < 0) {
        perror("Listen failed");
        close(lsock);
        exit(EXIT_FAILURE);
    }

    // Initialize connections
    for (int i = 0; i < MAX_CONNECTION; i++) {
        conn[i].status = 0;
        conn[i].Csock = -1;
        conn[i].Asock = -1;
        conn[i].Dsock = -1;
        conn[i].protocol_state = 0;
        conn[i].dataport = 0;
    }

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(lsock, &read_fds);
        max_sd = lsock;

        for (int i = 0; i < MAX_CONNECTION; i++) {
            if (conn[i].Csock > 0) {
                FD_SET(conn[i].Csock, &read_fds);
            }
            if (conn[i].Csock > max_sd) {
                max_sd = conn[i].Csock;
            }
        }

        activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);

        if ((activity < 0)) {
            perror("Select error");
        }

        if (FD_ISSET(lsock, &read_fds)) {
            if ((new_socket = accept(lsock, (struct sockaddr*)&client_addr, &client_len)) < 0) {
                perror("Accept error");
                exit(EXIT_FAILURE);
            }

            int idx;
            for (idx = 0; idx < MAX_CONNECTION; idx++) {
                if (conn[idx].Csock == -1) {
                    conn[idx].Csock = new_socket;
                    conn[idx].status = 1;
                    break;
                }
            }
            if (idx == MAX_CONNECTION) {
                fprintf(stderr, "Max connections reached\n");
                close(new_socket);
            }
        }

        for (int i = 0; i < MAX_CONNECTION; i++) {
            if (FD_ISSET(conn[i].Csock, &read_fds)) {
                int valread = read(conn[i].Csock, buffer, BUFFER_SIZE);
                if (valread == 0) {
                    close(conn[i].Csock);
                    conn[i].Csock = -1;
                    conn[i].status = 0;
                } else {
                    buffer[valread] = '\0';
                    handle_client_command(i, buffer + 11); // Assuming command starts at index 11
                }
            }
        }
    }

    close(lsock);
    return 0;
}
