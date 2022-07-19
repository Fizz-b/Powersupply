compile:
	gcc -Wall -g3 -fsanitize=address -pthread -lm server.c -o server
	gcc -Wall -g3 -fsanitize=address -pthread -lm client.c -o client

clean: 
	rm -f server client 