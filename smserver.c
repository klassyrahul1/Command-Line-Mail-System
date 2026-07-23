#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_USERS 100
#define MAX_CLIENTS 50
#define MAX_BODY 65536

typedef struct
{
    char username[21];
    char password[31];
    int next_id;
} User;

typedef enum
{
    STATE_WAIT_MODE,
    STATE_RECV_AUTH,
    STATE_RECV_CMD,
    STATE_SEND_FROM,
    STATE_SEND_TO,
    STATE_SEND_SUB,
    STATE_SEND_BODY
} ClientState;

typedef struct
{
    int fd;
    ClientState state;
    time_t connect_time;
    char buf[4096];
    int buf_len;
    char nonce[9];
    char logged_in_user[21];
    int auth_attempts;
    
    char sender[512];
    char recipients[50][21];
    int num_recipients;
    int to_count;
    char subject[512];
    char body[MAX_BODY + 1];
    int body_len;
    int body_overflow;
} ClientInfo;

User users[MAX_USERS];
int num_users = 0;
ClientInfo clients[MAX_CLIENTS];

// this function computes the djb2 hash
unsigned long djb2(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

// get the current system time
void get_timestamp(char *buf, size_t size)
{
    time_t now = time(NULL);
    strftime(buf, size, "[%Y-%m-%d %H:%M:%S]", localtime(&now));
}

// send the provided character string to specified client socket
void send_str(int fd, const char *msg)
{
    send(fd, msg, strlen(msg), 0);
}

// generate a random alphanumeric nonce string
void generate_nonce(char *nonce)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for(int i = 0; i < 8; i++) nonce[i] = charset[rand() % (sizeof(charset) - 1)];
    nonce[8] = '\0';
}

// find the index of a user in registered list
int get_user_idx(const char *username)
{
    for(int i = 0; i < num_users; i++)
    {
        if(strcasecmp(users[i].username, username) == 0) return i;
    }
    return -1;
}

// check if the given string contains only lowercase letters
int is_valid_username(const char *username)
{
    if(strlen(username) == 0 || strlen(username) > 20) return 0;
    for(int i = 0; username[i] != '\0'; i++)
    {
        // return zero if any character falls outside lowercase bounds
        if(username[i] < 'a' || username[i] > 'z') return 0;
    }
    return 1;
}

// verify the file name prefix only contains numeric digits
int is_numeric_id(const char *filename, int prefix_len)
{
    if(prefix_len <= 0) return 0;
    for(int i = 0; i < prefix_len; i++)
    {
        if(!isdigit(filename[i])) return 0;
    }
    return 1;
}

// compare two integer message ids for sorting directory listing
int cmp_id(const void *a, const void *b)
{
    return (*(int*)a - *(int*)b);
}

// initialize mailboxes directory and track next available message id
void init_mailboxes()
{
    mkdir("mailboxes", 0777);
    for(int i = 0; i < num_users; i++)
    {
        char path[100];
        snprintf(path, sizeof(path), "mailboxes/%s", users[i].username);
        mkdir(path, 0777);
        
        // open the user mailbox directory to scan existing messages
        DIR *d = opendir(path);
        int max_id = 0;
        struct dirent *dir;
        if(d)
        {
            while((dir = readdir(d)) != NULL)
            {
                int len = strlen(dir->d_name);
                if(len > 4 && strcmp(dir->d_name + len - 4, ".txt") == 0)
                {
                    if(is_numeric_id(dir->d_name, len - 4))
                    {
                        int id = atoi(dir->d_name);
                        if(id > max_id) max_id = id;
                    }
                }
            }
            closedir(d);
        }
        users[i].next_id = max_id + 1;
    }
}

// close the connected client socket and reset connection state
void reset_client(int i)
{
    close(clients[i].fd);
    clients[i].fd = 0;
}

// save the received email body and headers into file
void save_mail(ClientInfo *c)
{
    char date_str[50];
    time_t now = time(NULL);
    strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    char ts[50]; get_timestamp(ts, sizeof(ts));
    
    for(int i = 0; i < c->num_recipients; i++)
    {
        int u_idx = get_user_idx(c->recipients[i]);
        if(u_idx == -1) continue;
        
        char path[150];
        snprintf(path, sizeof(path), "mailboxes/%s/%d.txt", c->recipients[i], users[u_idx].next_id++);
        FILE *f = fopen(path, "w");
        // write the email contents to specific user mailbox file
        if(f)
        {
            fprintf(f, "From: %s\nTo: ", c->sender);
            for(int j=0; j<c->num_recipients; j++) fprintf(f, "%s%s", c->recipients[j], j==c->num_recipients-1 ? "" : ",");
            fprintf(f, "\nSubject: %s\nDate: %s\n---\n%s", c->subject, date_str, c->body);
            fclose(f);
        }
    }
    printf("%s Mail delivered from \"%s\" to [", ts, c->sender);
    for(int j = 0; j < c->num_recipients; j++) 
    {
        printf("%s%s", c->recipients[j], j == c->num_recipients - 1 ? "" : ",");
    }
    printf("] (%d recipient%s)\n", c->num_recipients, c->num_recipients == 1 ? "" : "s");
}

// process a single line of input received from client
void process_line(int c_idx, char *line)
{
    ClientInfo *c = &clients[c_idx];
    char ts[50]; get_timestamp(ts, sizeof(ts));
    
    int len = strlen(line);
    // remove trailing carriage return characters from received input line
    if(len > 0 && line[len-1] == '\r') line[len-1] = '\0';

    // close the connection if the client sends quit command
    if(strcmp(line, "QUIT") == 0)
    {
        send_str(c->fd, "BYE\r\n");
        printf("%s Client disconnected\n", ts);
        reset_client(c_idx);
        return;
    }

    switch(c->state)
    {
        case STATE_WAIT_MODE:
            if(strcmp(line, "MODE SEND") == 0)
            {
                c->state = STATE_SEND_FROM;
                printf("%s Client selected MODE SEND\n", ts);
                send_str(c->fd, "OK\r\n");
            }
            else if(strcmp(line, "MODE RECV") == 0)
            {
                c->state = STATE_RECV_AUTH;
                printf("%s Client selected MODE RECV\n", ts);
                send_str(c->fd, "OK\r\n");
                char msg[50];
                snprintf(msg, sizeof(msg), "AUTH REQUIRED %s\r\n", c->nonce);
                send_str(c->fd, msg);
            }
            else send_str(c->fd, "ERR Unknown mode\r\n");
            break;

        case STATE_RECV_AUTH:
            if(strncmp(line, "AUTH ", 5) == 0)
            {
                char u[21], h[30];
                int auth_success = 0;
                if(sscanf(line + 5, "%20s %29s", u, h) == 2)
                {
                    int idx = get_user_idx(u);
                    if(idx != -1)
                    {
                        char combined[60];
                        snprintf(combined, sizeof(combined), "%s%s", users[idx].password, c->nonce);
                        char calc_hash[30];
                        snprintf(calc_hash, sizeof(calc_hash), "%lu", djb2(combined));
                        
                        // authenticate the user using provided hash and stored password
                        if(strcmp(h, calc_hash) == 0)
                        {
                            auth_success = 1;
                            c->state = STATE_RECV_CMD;
                            strcpy(c->logged_in_user, users[idx].username); 
                            printf("%s Authentication successful for user %s\n", ts, users[idx].username);
                            char msg[50]; snprintf(msg, sizeof(msg), "OK Welcome %s\r\n", users[idx].username);
                            send_str(c->fd, msg);
                        }
                    }
                }
                
                if(!auth_success)
                {
                    c->auth_attempts++;
                    printf("%s Authentication failed\n", ts);
                    // disconnect the client if they fail authentication three times
                    if(c->auth_attempts >= 3)
                    {
                        send_str(c->fd, "ERR Too many failures\r\n");
                        reset_client(c_idx);
                    }
                    else send_str(c->fd, "ERR Authentication failed\r\n");
                }
            }
            else send_str(c->fd, "ERR Unknown command\r\n");
            break;

        case STATE_RECV_CMD:
            if(strcmp(line, "LIST") == 0)
            {
                char path[100];
                snprintf(path, sizeof(path), "mailboxes/%s", c->logged_in_user);
                DIR *d = opendir(path);
                int ids[1000];
                int count = 0;
                struct dirent *dir;
                if(d)
                {
                    // list all files in the mailbox directory and send
                    while((dir = readdir(d)) != NULL)
                    {
                        int dlen = strlen(dir->d_name);
                        if(dlen > 4 && strcmp(dir->d_name + dlen - 4, ".txt") == 0 && is_numeric_id(dir->d_name, dlen - 4))
                        {
                            ids[count++] = atoi(dir->d_name);
                        }
                    }
                    closedir(d);
                }
                // sort the retrieved message ids in ascending order safely
                qsort(ids, count, sizeof(int), cmp_id);
                char msg[50]; snprintf(msg, sizeof(msg), "OK %d messages\r\n", count);
                send_str(c->fd, msg);
                
                for(int i = 0; i < count; i++)
                {
                    char filepath[150];
                    snprintf(filepath, sizeof(filepath), "%s/%d.txt", path, ids[i]);
                    FILE *f = fopen(filepath, "r");
                    if(f)
                    {
                        char from[100]="", sub[100]="", date[100]="";
                        char fline[512];
                        // parse the email file to extract headers like sender
                        while(fgets(fline, sizeof(fline), f))
                        {
                            size_t flen = strlen(fline);
                            if(flen > 0 && fline[flen-1] == '\n') fline[flen-1] = '\0';
                            flen = strlen(fline);
                            if(flen > 0 && fline[flen-1] == '\r') fline[flen-1] = '\0';
                            
                            if(strncmp(fline, "From: ", 6) == 0) strcpy(from, fline+6);
                            else if(strncmp(fline, "Subject: ", 9) == 0) strcpy(sub, fline+9);
                            else if(strncmp(fline, "Date: ", 6) == 0) strcpy(date, fline+6);
                            else if(strcmp(fline, "---") == 0) break;
                        }
                        fclose(f);
                        char list_line[512];
                        snprintf(list_line, sizeof(list_line), "%d\t%s\t%s\t%s\r\n", ids[i], from, sub, date);
                        send_str(c->fd, list_line);
                    }
                }
                send_str(c->fd, ".\r\n");
            }
            else if(strcmp(line, "COUNT") == 0)
            {
                char path[100];
                snprintf(path, sizeof(path), "mailboxes/%s", c->logged_in_user);
                DIR *d = opendir(path);
                int count = 0;
                struct dirent *dir;
                if(d)
                {
                    while((dir = readdir(d)) != NULL)
                    {
                        int dlen = strlen(dir->d_name);
                        if(dlen > 4 && strcmp(dir->d_name + dlen - 4, ".txt") == 0 && is_numeric_id(dir->d_name, dlen - 4)) count++;
                    }
                    closedir(d);
                }
                char msg[50]; snprintf(msg, sizeof(msg), "OK %d\r\n", count);
                send_str(c->fd, msg);
            }
            else if(strncmp(line, "READ ", 5) == 0)
            {
                int id;
                if(sscanf(line + 5, "%d", &id) != 1) send_str(c->fd, "ERR No such message\r\n");
                else
                {
                    char path[150];
                    snprintf(path, sizeof(path), "mailboxes/%s/%d.txt", c->logged_in_user, id);
                    FILE *f = fopen(path, "r");
                    if(!f) send_str(c->fd, "ERR No such message\r\n");
                    else
                    {
                        send_str(c->fd, "OK\r\n");
                        char fline[512];
                        int in_body = 0;
                        // read the specified email file and send to client
                        while(fgets(fline, sizeof(fline), f))
                        {
                            size_t flen = strlen(fline);
                            if(flen > 0 && fline[flen-1] == '\n') fline[flen-1] = '\0';
                            flen = strlen(fline);
                            if(flen > 0 && fline[flen-1] == '\r') fline[flen-1] = '\0';
                            
                            if(in_body && fline[0] == '.') send_str(c->fd, "."); 
                            send_str(c->fd, fline);
                            send_str(c->fd, "\r\n");
                            if(strcmp(fline, "---") == 0) in_body = 1;
                        }
                        send_str(c->fd, ".\r\n");
                        fclose(f);
                        printf("%s User %s READ message %d\n", ts, c->logged_in_user, id);
                    }
                }
            }
            else if(strncmp(line, "DELETE ", 7) == 0)
            {
                int id;
                if(sscanf(line + 7, "%d", &id) != 1) send_str(c->fd, "ERR No such message\r\n");
                else
                {
                    char path[150];
                    snprintf(path, sizeof(path), "mailboxes/%s/%d.txt", c->logged_in_user, id);
                    if(remove(path) == 0)
                    {
                        send_str(c->fd, "OK Deleted\r\n");
                        printf("%s User %s DELETE message %d\n", ts, c->logged_in_user, id);
                    }
                    else send_str(c->fd, "ERR No such message\r\n");
                }
            }
            else send_str(c->fd, "ERR Unknown command\r\n");
            break;

        case STATE_SEND_FROM:
            if(strncmp(line, "FROM ", 5) == 0)
            {
                strncpy(c->sender, line + 5, sizeof(c->sender) - 1);
                c->sender[sizeof(c->sender) - 1] = '\0';
                c->state = STATE_SEND_TO;
                c->num_recipients = 0;
                c->to_count = 0;
                send_str(c->fd, "OK Sender accepted\r\n");
            }
            else send_str(c->fd, "ERR Bad sequence\r\n");
            break;

        case STATE_SEND_TO:
            if(strncmp(line, "TO ", 3) == 0)
            {
                c->to_count++; 
                char u[21]; sscanf(line + 3, "%20s", u);
                int idx = get_user_idx(u);
                // check if the provided recipient username exists in system
                if(idx != -1)
                {
                    int duplicate = 0;
                    for(int i = 0; i < c->num_recipients; i++)
                    {
                        if(strcmp(c->recipients[i], users[idx].username) == 0) duplicate = 1;
                    }
                    if(!duplicate) strcpy(c->recipients[c->num_recipients++], users[idx].username);
                    send_str(c->fd, "OK Recipient accepted\r\n");
                }
                else send_str(c->fd, "ERR No such user\r\n");
            }
            else if(strncmp(line, "SUB", 3) == 0 && (line[3] == ' ' || line[3] == '\0'))
            {
                if(c->to_count > 0)
                {
                    char *subj = line + 3;
                    while(*subj == ' ') subj++;
                    if(strlen(subj) == 0) strcpy(c->subject, "(no subject)");
                    else
                    {
                        strncpy(c->subject, subj, sizeof(c->subject) - 1);
                        c->subject[sizeof(c->subject) - 1] = '\0';
                    }
                    c->state = STATE_SEND_SUB;
                    send_str(c->fd, "OK Subject accepted\r\n");
                }
                else send_str(c->fd, "ERR Bad sequence\r\n");
            }
            else send_str(c->fd, "ERR Bad sequence\r\n");
            break;

        case STATE_SEND_SUB:
            if(strcmp(line, "BODY") == 0)
            {
                if(c->num_recipients == 0)
                {
                    send_str(c->fd, "ERR No valid recipients\r\n");
                    c->state = STATE_SEND_FROM; 
                }
                else
                {
                    c->body_len = 0;
                    c->body_overflow = 0;
                    c->body[0] = '\0';
                    c->state = STATE_SEND_BODY;
                    send_str(c->fd, "OK Send body, end with CRLF.CRLF\r\n");
                }
            }
            else send_str(c->fd, "ERR Bad sequence\r\n");
            break;

        case STATE_SEND_BODY:
            if(strcmp(line, ".") == 0)
            {
                if(c->body_overflow) send_str(c->fd, "ERR Body too large\r\n");
                else
                {
                    char msg[50];
                    snprintf(msg, sizeof(msg), "OK Delivered to %d mailboxes\r\n", c->num_recipients);
                    send_str(c->fd, msg);
                    save_mail(c);
                }
                c->state = STATE_SEND_FROM;
            }
            else
            {
                int input_len = strlen(line);
                if(line[0] == '.')
                {
                    line++; input_len--;
                } 
                
                if(!c->body_overflow)
                {
                    if(c->body_len + input_len + 1 <= MAX_BODY)
                    { 
                        // safely append the received line to the email body
                        strcpy(c->body + c->body_len, line);
                        c->body_len += input_len;
                        strcpy(c->body + c->body_len, "\n");
                        c->body_len += 1;
                    }
                    else c->body_overflow = 1; 
                }
            }
            break;
            
        default:
            send_str(c->fd, "ERR Bad sequence\r\n");
    }
}

int main(int argc, char *argv[])
{
    if(argc != 3)
    {
        printf("Usage: ./smserver <port> <userfile>\n"); return 1;
    }
    
    FILE *f = fopen(argv[2], "r");
    if(!f) return 1;
    
    char temp_user[21], temp_pass[31];
    while(fscanf(f, "%20s %30s", temp_user, temp_pass) == 2)
    {
        if(is_valid_username(temp_user))
        {
            strcpy(users[num_users].username, temp_user);
            strcpy(users[num_users].password, temp_pass);
            num_users++;
        }
    }
    fclose(f);
    
    init_mailboxes();
    srand(time(NULL) ^ getpid());

    for(int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = 0;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(atoi(argv[1])) };
    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    char ts[50]; get_timestamp(ts, sizeof(ts));
    printf("%s Server started on port %s\n", ts, argv[1]);
    printf("%s Loaded %d users from %s\n", ts, num_users, argv[2]);

    fd_set readfds;
    while(1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_sd = server_fd;

        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            if(clients[i].fd > 0)
            {
                FD_SET(clients[i].fd, &readfds);
                if(clients[i].fd > max_sd) max_sd = clients[i].fd;
            }
        }

        struct timeval tv = {1, 0}; 
        select(max_sd + 1, &readfds, NULL, NULL, &tv);

        time_t now = time(NULL);
        if(FD_ISSET(server_fd, &readfds))
        {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int new_socket = accept(server_fd, (struct sockaddr*)&caddr, &clen);

            int client_accepted = 0;
            for(int i = 0; i < MAX_CLIENTS; i++)
            {
                if(clients[i].fd == 0)
                {
                    clients[i].fd = new_socket;
                    clients[i].state = STATE_WAIT_MODE;
                    clients[i].connect_time = now;
                    clients[i].buf_len = 0;
                    clients[i].auth_attempts = 0;
                    generate_nonce(clients[i].nonce);
                    send_str(new_socket, "WELCOME SimpleMail v1.0\r\n");
                    get_timestamp(ts, sizeof(ts));
                    char cip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &caddr.sin_addr, cip, INET_ADDRSTRLEN);
                    printf("%s New connection from %s:%d\n", ts, cip, ntohs(caddr.sin_port));
                    client_accepted = 1;
                    break;
                }
            }
            if(!client_accepted)
            {
                send_str(new_socket, "ERR Server full\r\n");
                close(new_socket);
            }
        }

        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            if(clients[i].fd > 0)
            {
                if(clients[i].state == STATE_WAIT_MODE && (now - clients[i].connect_time) > 30)
                {
                    send_str(clients[i].fd, "ERR Timeout\r\n");
                    reset_client(i);
                    continue;
                }

                if(FD_ISSET(clients[i].fd, &readfds))
                {
                    int valread = recv(clients[i].fd, clients[i].buf + clients[i].buf_len, 
                                       sizeof(clients[i].buf) - clients[i].buf_len - 1, 0);
                    if(valread <= 0) reset_client(i);
                    else
                    {
                        clients[i].buf_len += valread;
                        clients[i].buf[clients[i].buf_len] = '\0';
                        
                        char *line_start = clients[i].buf;
                        char *line_end;
                        while((line_end = strstr(line_start, "\r\n")) != NULL)
                        {
                            *line_end = '\0';
                            process_line(i, line_start);
                            if(clients[i].fd == 0) break; 
                            line_start = line_end + 2;
                        }
                        
                        if(clients[i].fd > 0)
                        {
                            int rem = clients[i].buf_len - (line_start - clients[i].buf);
                            memmove(clients[i].buf, line_start, rem);
                            clients[i].buf_len = rem;
                        }
                    }
                }
            }
        }
    }
    return 0;
}
