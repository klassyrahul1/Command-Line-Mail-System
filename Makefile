all: smserver smclient

smserver: smserver.c
	gcc -Wall -Wextra -g -o smserver smserver.c

smclient: smclient.c
	gcc -Wall -Wextra -g -o smclient smclient.c

clean:
	rm -f smserver smclient
	rm -rf mailboxes
