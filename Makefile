all: smserver smclient

smserver: smserver.c
	gcc -Wall -g smserver.c -o smserver

smclient: smclient.c
	gcc -Wall -g smclient.c -o smclient

clean:
	rm -f smserver smclient
	rm -rf mailboxes/
