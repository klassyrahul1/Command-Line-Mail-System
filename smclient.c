#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define MAX_LINE 512

// Computes the DJB2 hash of a given string 
unsigned long djb2(const char *str) 
{
    unsigned long hash = 5381; 
    int c; 
    while((c = *str++)) hash = ((hash << 5) + hash) + c; // hash * 33 + c
    return hash; 
}

// Helper function to establish a fresh connection to the server
int connect_to_server(const char *ip, int port) 
{
    int sock = 0;
    struct sockaddr_in serv_addr;

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) 
    {
        perror("Invalid address or Address not supported");
        return -1;
    }

    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
    {
        perror("Connection Failed");
        return -1;
    }

    // Receive and consume the WELCOME greeting message from the server
    char buffer[MAX_LINE] = {0};
    recv(sock, buffer, MAX_LINE - 1, 0);
    
    return sock;
}

// Initiates the SMTP2 protocol interaction
void send_mail(const char *ip, int port) 
{
    int sock = connect_to_server(ip, port);
    if(sock < 0) return;

    send(sock, "MODE SEND\r\n", 11, 0);
    
    char buffer[MAX_LINE] = {0};
    recv(sock, buffer, MAX_LINE - 1, 0);
    
    if(strncmp(buffer, "OK", 2) == 0) 
    {
        // Implementation of SMTP2 dialogue goes here
        // Sequence: Prompt user -> FROM -> TO -> SUB -> BODY -> . -> QUIT
        printf("--- Sending a Mail ---\n");
        printf("From (your name): \n");
    }

    close(sock); 
}

// Initiates the SMP protocol interaction
void check_mailbox(const char *ip, int port) 
{
    int sock = connect_to_server(ip, port);
    if(sock < 0) return;

    // Send MODE declaration
    send(sock, "MODE RECV\r\n", 11, 0);
    
    char buffer[MAX_LINE] = {0};
    recv(sock, buffer, MAX_LINE - 1, 0);
    
    if(strncmp(buffer, "OK", 2) == 0) 
    {
        // Receive AUTH REQUIRED <nonce>
        memset(buffer, 0, MAX_LINE);
        recv(sock, buffer, MAX_LINE - 1, 0);
        
        // Implementation of SMP dialogue goes here
        // Sequence: Parse nonce -> Prompt user/pass -> Compute hash -> send AUTH
        printf("--- Checking Mailbox ---\n");
        printf("Username: \n");
    }

    close(sock); 
}

int main(int argc, char *argv[]) 
{
    if(argc != 3) 
    {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    // Initial connection just to test server reachability
    int init_sock = connect_to_server(server_ip, port);
    if(init_sock >= 0) 
    {
        printf("Connected to SimpleMail server.\n");
        close(init_sock); 
    } 
    else exit(1);

    int choice;
    char input_buffer[256];

    while(1) 
    {
        printf("1. Send a mail\n");
        printf("2. Check my mailbox\n");
        printf("3. Quit\n");
        printf("> ");
        
        if(!fgets(input_buffer, sizeof(input_buffer), stdin)) continue;
        choice = atoi(input_buffer);

        if(choice == 1) send_mail(server_ip, port);
        else if(choice == 2) check_mailbox(server_ip, port);
        else if(choice == 3) 
        {
            printf("Goodbye.\n");
            break;
        } 
        else printf("Invalid option. Please try again.\n");
    }

    return 0;
}
