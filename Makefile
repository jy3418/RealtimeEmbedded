# NAME: Justin Jaeo Youn
# EMAIL: jy3418@hotmail.com

CC = gcc
CFLAGS = -g -Wall -Wextra -lmraa -lm -lpthread
SSL = -lssl -lcrypto

build:
	$(CC) $(CFLAGS) -o lab4c_tcp lab4c_tcp.c
	$(CC) $(CFLAGS) $(SSL) -o lab4c_tls lab4c_tls.c

dist:
	tar -czvf lab4c-004906107.tar.gz lab4c_tcp.c lab4c_tls.c README Makefile
clean:
	rm -f lab4c_tcp lab4c_tls lab4c-004906107.tar.gz
