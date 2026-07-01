# SimpleMail

A minimal TCP-based mail system implemented in C, built for CS39006 (Computer Networks), Mini Project 2. SimpleMail defines two sub-protocols over a single connection-oriented server/client model:

- **SMTP2** — a simplified send protocol for delivering mail to a user's mailbox.
- **SMP** — a simplified mailbox-retrieval protocol, authenticated via a nonce + DJB2 hash challenge.

> **Status: Work in progress.** The connection setup, mode negotiation, and user-loading logic are functional. The actual SMTP2 and SMP session dialogues (message composition, delivery, authentication, mailbox listing) are stubbed out and not yet implemented — see [Implementation Status](#implementation-status) below.

## Repository Contents

| File | Description |
|---|---|
| `smserver.c` | Server: loads users, listens for connections, dispatches to SMTP2/SMP handlers |
| `smclient.c` | Client: connects to the server and drives the send/check-mailbox flows |
| `users.txt` | Sample user credential file (`username password`, one per line) |
| `Makefile` | Build rules for `smserver` and `smclient` |

## Building

```bash
make        # builds both smserver and smclient
make clean  # removes binaries and the mailboxes/ directory
```

Requires `gcc` on a POSIX system (uses BSD sockets — Linux/macOS/WSL).

## Running

**Start the server:**

```bash
./smserver <port> <userfile>
# e.g.
./smserver 5000 users.txt
```

On startup, the server loads credentials from `userfile` and creates a `mailboxes/<username>/` directory for each user (created under the directory `smserver` is run from).

**Run the client:**

```bash
./smclient <server_ip> <port>
# e.g.
./smclient 127.0.0.1 5000
```

The client presents a menu:

```
1. Send a mail
2. Check my mailbox
3. Quit
```

## Protocol Overview

Every session begins with a fresh TCP connection. The server sends a greeting, then the client declares its intended mode:

```
Server -> Client: WELCOME SimpleMail v1.0\r\n
Client -> Server: MODE SEND\r\n   (or MODE RECV\r\n)
Server -> Client: OK\r\n          (or ERR Unknown mode\r\n)
```

- **`MODE SEND`** hands off to the **SMTP2** session (`handle_smtp2_session` / `send_mail`), intended dialogue:
  `FROM -> TO -> SUB -> BODY -> . -> QUIT`, with delivery into the recipient's mailbox (body capped at 65536 bytes after any byte-stuffing/de-stuffing).

- **`MODE RECV`** hands off to the **SMP** session (`handle_smp_session` / `check_mailbox`), intended dialogue:
  `AUTH REQUIRED <nonce> -> client computes DJB2(nonce + password) -> AUTH <username> <hash> -> mailbox listing/retrieval`.

Authentication uses the DJB2 string hash (`hash = 5381; hash = hash*33 + c` per character), implemented identically in both client and server so the client can prove knowledge of a password without sending it in the clear.

## Implementation Status

**Done:**
- TCP socket setup, `bind`/`listen`/`accept` server loop
- Client connection + greeting consumption
- `MODE SEND` / `MODE RECV` negotiation and `OK`/`ERR` response
- User file parsing and per-user mailbox directory creation
- Shared DJB2 hash function

**TODO / stubbed:**
- `handle_smtp2_session`: FROM/TO/SUB/BODY dialogue, message framing/de-stuffing, mailbox delivery
- `handle_smp_session`: nonce generation, `AUTH REQUIRED <nonce>` challenge, credential verification
- `send_mail` (client): prompting for and sending FROM/TO/SUB/BODY, terminating `.`, `QUIT`
- `check_mailbox` (client): parsing the nonce, prompting username/password, sending `AUTH`, listing/reading mail
- Concurrency: server currently handles one connection fully before accepting the next (`select`/`poll` multiplexing planned but not yet wired in)
- 30-second timeout on the mode-declaration read
- Malformed-input and error-path handling throughout

## Known Issues

- Server `accept` loop is fully blocking — a slow or idle client stalls all other connections.
- No bounds/validation on lengths coming off the wire yet in the stub handlers.
- `users.txt` passwords are stored in plaintext on disk; only the wire exchange is intended to be hashed.

## License

Academic project — coursework submission for CS39006.
