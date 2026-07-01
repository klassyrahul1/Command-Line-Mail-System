#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define MAX_USERNAME_LENGTH 20      
#define MAX_PASSWORD_LENGTH 30      
#define MAX_LINE 512         
#define MAX_USERS 100 
#define BACKLOG 10 // Number of pending connections the queue will hold

typedef struct {
    char username[MAX_USERNAME_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
} User;

// Global user list
User user_list[MAX_USERS];
int user_count = 0;

// Computes the DJB2 hash of a given string
unsigned long djb2(const char *str) 
{
    unsigned long hash = 5381; 
    int c; 
    while((c = *str++)) hash = ((hash << 5) + hash) + c; // hash * 33 + c
    return hash; 
}

// Creates the base mailboxes directory if it doesn't exist
void ensure_mailbox_directory() 
{
    struct stat st = {0};
    if(stat("mailboxes", &st) == -1) 
    {
        if(mkdir("mailboxes", 0700) != 0) 
        {
            perror("Failed to create base mailboxes directory");
            exit(1);
        }
    }
}

// Loads users from the specified file 
int load_users(const char *filename) 
{
    FILE *file = fopen(filename, "r");
    if(!file) 
    {
        perror("Error opening user file"); 
        return -1;
    }

    char line[MAX_USERNAME_LENGTH + MAX_PASSWORD_LENGTH + 10];
    ensure_mailbox_directory();

    while(fgets(line, sizeof(line), file)) 
    {
        if(user_count >= MAX_USERS) 
        {
            printf("Warning: Maximum user limit reached.\n");
            break;
        }

        // Parse username and password 
        char *username = strtok(line, " \t\r\n");
        char *password = strtok(NULL, " \t\r\n");

        if(username && password) 
        {
            strncpy(user_list[user_count].username, username, MAX_USERNAME_LENGTH);
            user_list[user_count].username[MAX_USERNAME_LENGTH] = '\0';
            
            strncpy(user_list[user_count].password, password, MAX_PASSWORD_LENGTH);
            user_list[user_count].password[MAX_PASSWORD_LENGTH] = '\0';

            // Ensure mailbox directory exists for this user
            char dir_path[256];
            snprintf(dir_path, sizeof(dir_path), "mailboxes/%s", username);
            
            struct stat st = {0};
            if(stat(dir_path, &st) == -1) 
            {
                if(mkdir(dir_path, 0700) != 0) fprintf(stderr, "Failed to create directory for user %s\n", username);
            }
            user_count++;
        }
    }

    fclose(file);
    return user_count;
}

// Handles the SMTP2 (Send) protocol interaction
void handle_smtp2_session(int client_fd) 
{
    // Implementation of sequence: FROM -> TO -> SUB -> BODY -> delivery
    // Max body size is 65536 bytes after de-stuffing
    printf("Started SMTP2 session.\n");
}

// Handles the SMP (Receive) protocol interaction. 
void handle_smp_session(int client_fd) 
{
    // Implementation of AUTH REQUIRED <nonce> challenge. 
    printf("Started SMP session.\n");
}

// Initial connection handler. Greets the client and determines the mode.
void handle_new_connection(int client_fd) {
    char buffer[MAX_LINE];
    
    const char *greeting = "WELCOME SimpleMail v1.0\r\n"; 
    send(client_fd, greeting, strlen(greeting), 0);
    
    // 2. Wait for MODE declaration (SEND or RECV) 
    // Implementation of 30-second timeout logic 
    memset(buffer, 0, MAX_LINE);
    int bytes_read = recv(client_fd, buffer, MAX_LINE - 1, 0);
    
    if(bytes_read > 0) 
    {
        if(strncmp(buffer, "MODE SEND", 9) == 0) 
        {
            const char *ok_resp = "OK\r\n";
            send(client_fd, ok_resp, strlen(ok_resp), 0);
            handle_smtp2_session(client_fd); 
        } 
        else if(strncmp(buffer, "MODE RECV", 9) == 0) 
        {
            const char *ok_resp = "OK\r\n";
            send(client_fd, ok_resp, strlen(ok_resp), 0);
            handle_smp_session(client_fd); 
        } 
        else 
        {
            const char *err_resp = "ERR Unknown mode\r\n"; 
            send(client_fd, err_resp, strlen(err_resp), 0);
        }
    }
    
    close(client_fd);
}


int main(int argc, char *argv[]) 
{
    if(argc != 3) 
    {
        fprintf(stderr, "Usage: %s <port> <userfile>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    const char *userfile = argv[2];

    // Load user list
    if(load_users(userfile) <= 0) 
    {
        fprintf(stderr, "Failed to load users or userfile is empty/missing.\n"); 
        exit(1);
    }

    // Setup TCP socket
    int server_fd;
    struct sockaddr_in server_addr;

    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    {
        perror("Socket creation failed");
        exit(1);
    }

    // Allow port reuse
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) 
    {
        perror("setsockopt");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
    {
        perror("Bind failed");
        exit(1);
    }

    if(listen(server_fd, BACKLOG) < 0) 
    {
        perror("Listen failed");
        exit(1);
    }

    // Basic loop into a select()/poll() multiplexer to handle concurrency (currently blocking)
    while (1) 
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        
        if(client_fd < 0) 
        {
            perror("Accept failed");
            continue;
        }

        printf("[YYYY-MM-DD HH:MM:SS] New connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port)); 
        // Will be replaced by non-blocking/multiplexed logic.
        handle_new_connection(client_fd);
    }

    close(server_fd);
    return 0;
}