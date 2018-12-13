all:
	gcc server.c -O2 -lpthread -o server

clean:
	rm -rf server
