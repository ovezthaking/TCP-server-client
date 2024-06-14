#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define MAX_DATA 100000

uint32_t data[MAX_DATA]; 
uint32_t histogram[16];     

int option_showcontent = 1;


int simpleprotocol_create_frame(char* buffer, int buffer_size, char* name, char command, char* options) 
{
    int framelen;

    if (name == NULL)
        return (-1);

    if (options == NULL)
        return (-2);

    framelen = snprintf(buffer,buffer_size ,"@%s!%c:%s#", name, command, options);
    if (option_showcontent)
        printf("framelen= %d   %d   %s\n", framelen, (int)strlen(buffer), buffer);
    return (framelen);
}

int simpleprotocol_checkmessage(char* buffer, int size, char** name, char* command, char**options) 
{
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

void send_protocol(int sock, const char *name, char command, const char *params) {
    char buffer[BUFFER_SIZE];
    int framelen = simpleprotocol_create_frame(buffer, sizeof(buffer), (char*)name, command, (char*)params);
    if (framelen > 0) {
        if (send(sock, buffer, framelen, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
            perror("send");
            close(sock);
            exit(EXIT_FAILURE);
        }
        printf("Wysłano komendę: %s\n", buffer); // Debug
    } else {
        printf("Błąd tworzenia ramki do wysłania\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <name> <IP> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
 
    const char *name = argv[1];
    
    char modified_name[9];
    char old_name[strlen(name)];
    strcpy(old_name, name);

    if (strlen(old_name) < 8) 
    {
        memset(modified_name, '0', sizeof(modified_name));
        strncpy(modified_name, old_name, strlen(old_name));
    } 
    else if (strlen(old_name) > 8)
    {
        printf("Nazwa za długa, max 8 znaków\n");
        exit(EXIT_FAILURE);
    }
    else 
    {
        strncpy(modified_name, old_name, sizeof(modified_name) - 1);
    }
    modified_name[sizeof(modified_name) - 1] = '\0';

    
    const char *ip = argv[2];
    int port = atoi(argv[3]);
    

    while (1) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &server_addr.sin_addr);

        if (connect(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
            perror("connect");
            close(sock);
            sleep(1);
            continue;
        }

        // Wysłanie komendy N
        send_protocol(sock, modified_name, 'N', "");

        char buffer[BUFFER_SIZE];
        char *name;
        char command;
        char *options;

        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            perror("recv");
            close(sock);
            sleep(1);
            continue;
        }
        buffer[n] = '\0';

        
        if (simpleprotocol_checkmessage(buffer, n, &name, &command, &options) == 0) {
            printf("Odebrano komendę: %c\n", command);
            
            if (command == 'P') {
                int data_port = atoi(options);
                printf("Otrzymano port danych: %d\n", data_port);

                // Połączenie do portu danych
                int data_sock = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in data_addr;
                data_addr.sin_family = AF_INET;
                data_addr.sin_port = htons(data_port);
                inet_pton(AF_INET, ip, &data_addr.sin_addr);

                if (connect(data_sock, (struct sockaddr *) &data_addr, sizeof(data_addr)) < 0) {
                    perror("connect");
                    close(data_sock);
                } else {
                    printf("Połączono z portem danych %d\n", data_port);
                    n = recv(data_sock, buffer, sizeof(buffer) - 1, 0);
                    if (n <= 0) {
                        perror("recv");
                        close(data_sock);
                    } else {
                        
                        printf("Otrzymano dane \n");
                    
                        uint32_t histogram[16] = {23,42,33,13,63,1723,23,93,23,43,12,23,81,4,3,12}; 
                               
                        char results[BUFFER_SIZE];
                        snprintf(results, sizeof(results), "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                                 histogram[0], histogram[1], histogram[2], histogram[3],
                                 histogram[4], histogram[5], histogram[6], histogram[7],
                                 histogram[8], histogram[9], histogram[10], histogram[11],
                                 histogram[12], histogram[13], histogram[14], histogram[15]);
                    
                        send_protocol(data_sock, modified_name, 'R', results);
                        printf("Wysłano wyniki\n");
                        
                    }
                    
                    
                    close(data_sock);
                }
                int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
                    if (n <= 0) {
                    perror("recv");
                    close(sock);
                    sleep(1);
                    continue;
                    }
                buffer[n] = '\0';
                if (simpleprotocol_checkmessage(buffer, n, &name, &command, &options) == 0) {
                printf("Odebrano komendę: %c\n", command);
                if (command == 'D') {
                    printf("Otrzymano komendę D, zamykanie połączenia.\n");
                    close(sock);
                    sleep(1);
                    continue;
                } else if (command == 'X') {
                    printf("Otrzymano komendę X, zakończenie programu.\n");
                    close(sock);
                    break;
                }
                
                }


            
        } else {
            printf("Błąd parsowania wiadomości\n");
        }
        
        close(sock);
        
    }

    return 0;
}
}
