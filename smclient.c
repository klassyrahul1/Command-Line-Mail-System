#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

void strip_crlf(char *str)
{
    str[strcspn(str, "\r\n")] = 0;
}

// compute the djb2 hash string
unsigned long djb2(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// read characters from socket until a newline is found
int read_line(int sock, char *out_buf, int max_len)
{
    int i = 0;
    char c;
    while(i < max_len - 1)
    {
        // read bytes from socket and check for error conditions
        int bytes_read = recv(sock, &c, 1, 0);
        if(bytes_read <= 0) return -1;
        out_buf[i++] = c;
        if(c == '\n') break;
    }
    out_buf[i] = '\0';
    strip_crlf(out_buf);
    return i;
}

// send formatted command string over the connected tcp socket
void send_cmd(int sock, const char *cmd)
{
    if(send(sock, cmd, strlen(cmd), 0) < 0)
    {
        perror("send");
        exit(1);
    }
}

// establish tcp connection to the server using provided ip
int connect_to_server(const char *ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr = {.sin_family=AF_INET, .sin_port=htons(port)};
    
    inet_pton(AF_INET, ip, &saddr.sin_addr);
    
    if(connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) return -1;
    
    char buf[1024];
    // connect to the remote server and check for success
    if(read_line(sock, buf, sizeof(buf)) < 0) return -1;
    return sock;
}

// this function handles sending an email using smtp protocol
void handle_mode_send(int sock)
{
    char buf[1024];
    char input[514]; 

    send_cmd(sock, "MODE SEND\r\n");
    read_line(sock, buf, sizeof(buf)); 
    
    printf("From (your name): ");
    fgets(input, 510, stdin); strip_crlf(input);
    snprintf(buf, sizeof(buf), "FROM %s\r\n", input);
    send_cmd(sock, buf);
    read_line(sock, buf, sizeof(buf)); 
    
    int recipients = 0;
    while(1)
    {
        printf("To (recipient username, empty line to finish): ");
        fgets(input, 510, stdin); strip_crlf(input);
        if(strlen(input) == 0)
        {
            // stop adding recipients if the user enters empty line
            if(recipients > 0) break;
            printf("Error: Provide at least one recipient.\n");
            continue;
        }
        snprintf(buf, sizeof(buf), "TO %s\r\n", input);
        // send recipient username to the server and check response
        send_cmd(sock, buf);
        if(read_line(sock, buf, sizeof(buf)) < 0) return;
        
        // check if server successfully accepted provided recipient username string
        if(strstr(buf, "OK"))
        {
            printf("-> Recipient '%s' accepted.\n", input);
            recipients++;
        }
        else
        {
            printf("-> Error: user '%s' does not exist on this server.\n", input);
        }
    }
    
    // prompt the user to enter the subject of email
    printf("Subject: ");
    fgets(input, 510, stdin); strip_crlf(input);
    if(strlen(input) == 0) snprintf(buf, sizeof(buf), "SUB\r\n");
    else snprintf(buf, sizeof(buf), "SUB %s\r\n", input);
    send_cmd(sock, buf);
    read_line(sock, buf, sizeof(buf));
    
    printf("Body (type '.' on a line by itself to finish):\n");
    // send the body command to indicate start of content
    send_cmd(sock, "BODY\r\n");
    read_line(sock, buf, sizeof(buf));
    
    if(strstr(buf, "ERR"))
    {
        printf("Error: %s\n", buf);
        send_cmd(sock, "QUIT\r\n");
        return;
    }
    
    // read lines from user input until single dot entered
    while(1)
    {
        fgets(input, 510, stdin); strip_crlf(input);
        if(strcmp(input, ".") == 0)
        {
            send_cmd(sock, ".\r\n");
            break;
        }
        // apply byte stuffing logic to lines starting with dot
        if(input[0] == '.') send_cmd(sock, "."); 
        send_cmd(sock, input);
        send_cmd(sock, "\r\n");
    }
    
    read_line(sock, buf, sizeof(buf));
    // parse the server response to check delivered mail count
    if(strstr(buf, "OK Delivered"))
    {
        int delivered = 0;
        sscanf(buf, "OK Delivered to %d", &delivered);
        printf("Mail delivered to %d recipient%s.\n", delivered, delivered == 1 ? "" : "s");
    }
    else printf("Error: %s\n", buf);
    
    send_cmd(sock, "QUIT\r\n");
    read_line(sock, buf, sizeof(buf));
}

// this function handles receiving and managing emails in mailbox
void handle_mode_recv(int sock)
{
    char buf[1024];

    send_cmd(sock, "MODE RECV\r\n");
    read_line(sock, buf, sizeof(buf));

    read_line(sock, buf, sizeof(buf)); 
    char nonce[20];
    // extract the nonce token sent by server for authentication
    if(sscanf(buf, "AUTH REQUIRED %s", nonce) != 1) return;
    
    int authenticated = 0;
    char user[50];
    for(int attempts = 0; attempts < 3; attempts++)
    {
        char pass[50];
        printf("Username: "); fgets(user, sizeof(user), stdin); strip_crlf(user);
        printf("Password: "); fgets(pass, sizeof(pass), stdin); strip_crlf(pass);
        
        // compute combined string using password and nonce for login
        char combined[100]; snprintf(combined, sizeof(combined), "%s%s", pass, nonce);
        char cmd[200]; snprintf(cmd, sizeof(cmd), "AUTH %s %lu\r\n", user, djb2(combined));
        send_cmd(sock, cmd);
        
        read_line(sock, buf, sizeof(buf));
        if(strstr(buf, "OK Welcome"))
        {
            authenticated = 1; break;
        }
        printf("Authentication failed.\n");
        if(strstr(buf, "ERR Too many"))
        {
            printf("Too many failures. Connection closed by server.\n");
            return;
        }
    }
    
    if(!authenticated) return;
    printf("Welcome, %s!\n", user);
    
    while(1)
    {
        send_cmd(sock, "COUNT\r\n");
        read_line(sock, buf, sizeof(buf));
        int count = 0; sscanf(buf, "OK %d", &count);
        
        printf("Mailbox for %s (%d message%s)\n1. List all messages\n2. Read a message\n3. Delete a message\n4. Logout\n> ", user, count, count == 1 ? "" : "s");
        char opt[10]; fgets(opt, sizeof(opt), stdin);
        int choice = atoi(opt);
        
        if(choice == 1)
        {
            send_cmd(sock, "LIST\r\n");
            read_line(sock, buf, sizeof(buf)); 
            printf("ID From Subject Date\n");
            printf("--- ---- ------- ----\n");
            while(read_line(sock, buf, sizeof(buf)) >= 0 && strcmp(buf, ".") != 0)
            {
                char id[10]="", from[50]="", sub[50]="", date[50]="";
                sscanf(buf, "%[^\t]\t%[^\t]\t%[^\t]\t%[^\t]", id, from, sub, date);
                printf("%s %s %s %s\n", id, from, sub, date);
            }
        }
        else if(choice == 2)
        {
            printf("Enter message ID: "); fgets(buf, sizeof(buf), stdin); strip_crlf(buf);
            int id = atoi(buf);
            char cmd[32]; snprintf(cmd, sizeof(cmd), "READ %d\r\n", id);
            send_cmd(sock, cmd);
            read_line(sock, buf, sizeof(buf));
            if(strstr(buf, "OK"))
            {
                while(read_line(sock, buf, sizeof(buf)) >= 0 && strcmp(buf, ".") != 0)
                {
                    printf("%s\n", buf[0] == '.' ? buf + 1 : buf); 
                }
            }
            else printf("%s\n", buf);
        }
        else if(choice == 3)
        {
            printf("Enter message ID: "); fgets(buf, sizeof(buf), stdin); strip_crlf(buf);
            int id = atoi(buf);
            char cmd[32]; snprintf(cmd, sizeof(cmd), "DELETE %d\r\n", id);
            send_cmd(sock, cmd);
            read_line(sock, buf, sizeof(buf));
            if(strstr(buf, "OK Deleted")) printf("Message %d deleted.\n", id);
            else printf("%s\n", buf);
        }
        else if(choice == 4)
        {
            send_cmd(sock, "QUIT\r\n");
            read_line(sock, buf, sizeof(buf));
            printf("Logged out.\n"); 
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("Usage: ./smclient <server_ip> <port>\n"); return 1;
    }
    
    int sock = -1;

    while(1)
    {
        if(sock == -1)
        {
            sock = connect_to_server(argv[1], atoi(argv[2]));
            if(sock < 0)
            {
                printf("Failed to connect to server.\n");
                return 1;
            }
            printf("Connected to SimpleMail server.\n");
        }
        
        printf("1. Send a mail\n2. Check my mailbox\n3. Quit\n> ");
        fflush(stdout);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int max_sd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if(activity < 0)
        {
            perror("select error");
            break;
        }

        if(FD_ISSET(sock, &readfds))
        {
            char dummy_buf[1024];
            int r = recv(sock, dummy_buf, sizeof(dummy_buf), 0);
            if(r <= 0)
            {
                printf("\nSession timed out. Connection closed by server.\n");
                close(sock);
                break;
            }
        }

        if(FD_ISSET(STDIN_FILENO, &readfds))
        {
            char opt[10]; 
            if(!fgets(opt, sizeof(opt), stdin)) break;
            int choice = atoi(opt);
            
            if(choice == 1)
            {
                handle_mode_send(sock);
                close(sock);
                sock = -1;
            }
            else if(choice == 2)
            {
                handle_mode_recv(sock);
                close(sock);
                sock = -1;
            }
            else if(choice == 3)
            {
                send_cmd(sock, "QUIT\r\n");
                close(sock);
                break;
            }
            else printf("Invalid option.\n");
        }
    }
    printf("Goodbye.\n");
    return 0;
}
