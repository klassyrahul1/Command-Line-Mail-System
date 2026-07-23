Name: Gampa Reshwik
Roll No: 23CS30020

SimpleMail Implementation Details

The project consists of a server (smserver.c) and an interactive client (smclient.c) 
that communicate over TCP using the custom SMTP2 and SMP protocols. 
Both programs are written in C and strictly adhere to the provided specifications.

Compilation and Execution:
I have included a Makefile to streamline the build process.
To compile: run "make"
To clean up: run "make clean"
Server Execution: ./smserver <port> <userfile>
Client Execution: ./smclient <server_ip> <port>

Server Design & Concurrency (smserver.c):
To handle multiple clients simultaneously without blocking, I implemented an event-driven architecture using select().

State Machine: Each client connection is managed via a dedicated state machine (ClientState) 
tracking exactly where they are in the protocol sequence (e.g., waiting for mode, receiving TO, sending BODY). 
This ensures out-of-sequence commands are caught and rejected immediately.

Mailbox Management: On startup, the server automatically reads the users.txt file and 
creates a "mailboxes/" directory along with a subdirectory for each user.

ID Generation: To ensure message IDs are monotonically increasing and never reused (even after deletions), 
the server scans each user's directory on startup to find the highest existing ID and resumes from there.

Timeout Handling: The server records the connection time of each client. During the main select() loop, 
it checks if any client in the initial mode-selection state has been idle for more than 30 seconds. 
If so, it drops the connection to prevent resource exhaustion.

Client Design & User Interface (smclient.c):
The client provides a clean, interactive terminal UI that completely abstracts the raw protocol commands from the user.

COUNT Command Integration: While COUNT is not exposed as a standalone numbered option in the user menu, 
it is fully implemented and utilized under the hood. The client automatically issues the COUNT command upon a successful login, 
and after returning to the mailbox menu, to dynamically display the total number of messages in the UI prompt 
(e.g., Mailbox for alice (3 messages)).

Modularity: The UI logic is separated from the networking logic. 
Functions like handle_mode_send() and handle_mode_recv() manage the specific protocol handshakes, 
making the code much easier to read and maintain.

Reconnection Logic: As per the protocol, after a mail is sent or a mailbox session ends, the client sends QUIT, closes the socket, 
and returns to the main menu. If the user selects another option, the client seamlessly opens a fresh TCP connection.

Idle Disconnect Detection: I implemented a select() loop in the client's main menu. 
It listens to both the standard input (user typing) and the server socket. 
If the server terminates the connection due to the 30-second timeout, 
the client detects the socket closure instantly and exits gracefully rather than hanging.

String Parsing: I implemented a robust line-reading function that reliably strips both \r and \n. 
This prevents trailing carriage returns from breaking string comparisons or output formatting.

Dot-Stuffing (Byte Stuffing): In SMTP2, if the user types a message line beginning with a period, 
the client automatically prepends an extra period before sending. 
The server correctly identifies this and removes the extra period before writing to the disk.

DJB2 Authentication: The SMP protocol implements the required challenge-response authentication. 
The server generates a random 8-character nonce, and the client computes the DJB2 hash of the concatenated password and nonce. 
The server enforces a strict 3-attempt limit for invalid logins.

Error Handling: The server validates all inputs. It checks if users exist before accepting TO commands, 
enforces the 65,536-byte limit on the email body, and rejects malformed protocol sequences. 
Empty subjects are properly caught and stored as "(no subject)".
