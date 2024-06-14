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

// Definicje stałych
#define NAME_SIZE 8       
#define BUFFER_SIZE 1024   
#define MAX_CONNECTIONS 10 
#define MAX_DATA 10000   


struct CALCDATA {
    uint32_t data[MAX_DATA]; 
};


struct conninfo {
    int status;              // Status połączenia: 0 - nieaktywne, 1 - aktywne
    char name[NAME_SIZE + 1];// Nazwa klienta (dodatkowy bajt na '\0')
    int Csock;               // Gniazdo komunikacyjne z klientem
    int protocol_state;      // 1 - oczekiwanie na komendę N, 2 - oczekiwanie na połączenie danych
    int Asock;               
    int Dsock;               
    int dataport;            
    struct CALCDATA data;    
};

// Główne gniazdo nasłuchujące
int lsock;
// Tablica połączeń
struct conninfo conn[MAX_CONNECTIONS];

// Funkcja do czyszczenia histogramu
void clear_histogram(uint32_t *histogram) {
    memset(histogram, 0, sizeof(uint32_t) * 16); 
}

// Funkcja do ładowania histogramu
void load_histogram(uint32_t *histogram, uint32_t value) {
    uint32_t mask = 1;
    for (int i = 0; i < 16; i++) {
        if (value & mask) histogram[i]++; 
        mask <<= 1;                      
    }
}

// Funkcja do generowania danych
void create_data(struct CALCDATA *cdata) {
    if (cdata != NULL) {
        for (int i = 0; i < MAX_DATA; i++) {
            uint32_t v = (uint32_t) rand() ^ (uint32_t) rand(); 
            cdata->data[i] = v & 0x0000FFFF; 
        }
    }
}
int option_showcontent=1;

int simpleprotocol_create_frame(char* buffer, int buffer_size, char* name, char command, char* options) {
    int framelen;

    if (name == NULL)
        return (-1);
    if (options == NULL)
        return (-2);

    framelen = snprintf(buffer, buffer_size, "@%s!%c:%s#", name, command, options);
    if (option_showcontent)
        printf("framelen= %d   %d   %s\n", framelen, (int)strlen(buffer), buffer);
    return (framelen);
}

int simpleprotocol_checkmessage(char* buffer, int size, char** name, char* command, char** options) {
    *command = ' ';

    int jest_at = 0;
    int jest_wykrzyknik = 0;
    int jest_dwukropek = 0;
    int jest_hash = 0;
    int idx_dwukropek = 0;
    int idx_at = 0;

    int maxlen = size;

    for (int i = 0; i < maxlen; i++) {
        switch (buffer[i]) {
            case '@':
                jest_at++;
                idx_at = i;
                break;
            case '!':
                jest_wykrzyknik++;
                buffer[i] = '\0';  //zamieniamy ! na \0 żeby było zakończenie stringa name
                *name = &buffer[idx_at + 1];
                *command = buffer[i + 1];
                break;
            case ':':
                jest_dwukropek++;
                idx_dwukropek = i;
                break;
            case '#':
                jest_hash++;
                buffer[i] = '\0';  //zamieniamy # na \0 żeby było zakończenie stringa options
                *options = &buffer[idx_dwukropek + 1];
                break;
        }
    }

    int errors = 0;

    if (jest_at < 1) {
        printf("Lack of '@'\n");
        errors++;
    };
    if (jest_at > 1) {
        printf("Too match '@'\n");
        errors++;
    };

    if (jest_wykrzyknik < 1) {
        printf("Lack of '!'\n");
        errors++;
    };
    if (jest_wykrzyknik > 1) {
        printf("Too match '!'\n");
        errors++;
    };

    if (jest_dwukropek < 1) {
        printf("Lack of ':'\n");
        errors++;
    };
    if (jest_dwukropek > 1) {
        printf("Too match ':'\n");
        errors++;
    };

    if (jest_hash < 1) {
        printf("Lack of '#'\n");
        errors++;
    };
    if (jest_hash > 1) {
        printf("Too match '#'\n");
        errors++;
    };

    if (errors > 0)
        return (-10);
    return (0);
}

// Funkcja do wysyłania komend protokołu do klienta
void send_protocol(int sock, const char *name, char command, const char *params) {
    char buffer[BUFFER_SIZE];
    int framelen = simpleprotocol_create_frame(buffer, sizeof(buffer), (char*)name, command, (char*)params);
    if (framelen > 0) {
        if (send(sock, buffer, framelen, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
            perror("send");
            close(sock);
            sleep(1); 
        }
        printf("Wysłano komendę: %s\n", buffer); // Debug
    } else {
        printf("Błąd tworzenia ramki do wysłania\n");
    }
}

void handle_client(int idx) {
    struct conninfo *c = &conn[idx];
    char buffer[BUFFER_SIZE];
    char *name;
    char command;
    char *options;
    
    int n = recv(c->Csock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        if (simpleprotocol_checkmessage(buffer, n, &name, &command, &options) == 0) {
            printf("Odebrano komendę: %c\n", command);
            if (command == 'N') {
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
                if (bind(c->Asock, (struct sockaddr *) &data_addr, sizeof(data_addr)) < 0) {
                    perror("bind");
                    close(c->Asock);
                    close(c->Csock);
                    c->status = 0;
                    sleep(1);
                }
                listen(c->Asock, 1);
            }
        } else {
            printf("Błąd parsowania wiadomości\n");
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
        
        if (send(c->Dsock, c->data.data, sizeof(c->data.data), MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
            perror("send");
            close(c->Dsock);
            close(c->Asock);
            c->status = 0;
            sleep(1); 
        }
        printf("Wysłano dane do klienta\n");

        char buffer[BUFFER_SIZE];
        int n = recv(c->Dsock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            char *name;
            char command;
            char *options;
            if (simpleprotocol_checkmessage(buffer, n, &name, &command, &options) == 0) {
                printf("Odebrano wyniki od klienta\n");
                printf(options, '\n');
                uint32_t histogram[16];
                clear_histogram(histogram);
                sscanf(options, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                       &histogram[0], &histogram[1], &histogram[2], &histogram[3],
                       &histogram[4], &histogram[5], &histogram[6], &histogram[7],
                       &histogram[8], &histogram[9], &histogram[10], &histogram[11],
                       &histogram[12], &histogram[13], &histogram[14], &histogram[15]);
                    
        
                printf("\nWyniki poprawne, zamykam połączenie\n");
                send_protocol(c->Csock, "00000000", 'D', "0");
                
                close(c->Dsock);
                close(c->Asock);
                c->status = 0;
            } else {
                printf("Błąd parsowania wiadomości\n");
            }
        } else if (n == 0) {
            printf("Klient zamknął połączenie.\n");
            close(c->Dsock);
            close(c->Asock);
            c->status = 0;
        } else {
            perror("recv");
            close(c->Dsock);
            close(c->Asock);
            c->status = 0;
        }
    } else {
        perror("accept");
    }
}


// Główna funkcja programu
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]); 
        exit(EXIT_FAILURE); 
    }

    int port = atoi(argv[1]); 

    lsock = socket(AF_INET, SOCK_STREAM, 0); // Tworzenie gniazda nasłuchującego
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(lsock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(lsock);
        exit(EXIT_FAILURE);
    }
    listen(lsock, 10); // Nasłuchiwanie połączeń
    printf("Serwer uruchomiony na porcie %d. Oczekiwanie na połączenia...\n", port);

    fd_set read_fds; // Zestaw gniazd do sprawdzenia
    while (1) {
        FD_ZERO(&read_fds); // Zerowanie zestawu gniazd
        FD_SET(lsock, &read_fds); // Dodanie gniazda nasłuchującego do zestawu
        int max_sd = lsock;

        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (conn[i].status == 1) {
                FD_SET(conn[i].Csock, &read_fds); // Dodanie aktywnego gniazda komunikacyjnego do zestawu
                if (conn[i].Csock > max_sd) max_sd = conn[i].Csock;
            }
            if (conn[i].protocol_state == 2) {
                FD_SET(conn[i].Asock, &read_fds); // Dodanie gniazda nasłuchującego na połączenie danych do zestawu
                if (conn[i].Asock > max_sd) max_sd = conn[i].Asock;
            }
        }

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL); // Sprawdzanie aktywności na gniazdach


        if (FD_ISSET(lsock, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_socket = accept(lsock, (struct sockaddr *) &client_addr, &addr_len); // Akceptowanie nowego połączenia
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
                perror("accept"); // Obsługa błędów akceptowania połączenia
            }
        }

        // Obsługa aktywnych połączeń
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            if (conn[i].status == 1 && FD_ISSET(conn[i].Csock, &read_fds)) {
                handle_client(i); // Obsługa komendy od klienta
            }
            if (conn[i].protocol_state == 2 && FD_ISSET(conn[i].Asock, &read_fds)) {
                handle_data_connection(i); // Obsługa połączenia danych
            }
        }
    }

    return 0;
}
