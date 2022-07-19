
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#define BUFF_SIZE 8192
pthread_t lobby_thread; // send message thread
pthread_t recv_msg_thread;
int client_sock;
void str_overwrite_stdout()
{
	printf("\r%s", "> ");
	fflush(stdout);
}
void my_handler(int s)
{
	send(client_sock, "kill", 5, 0);
	printf("Caught end signal: %d\n", s);
	close(client_sock);
	kill(0, SIGKILL);
	exit(1);
}
void menu()
{
	printf(
		"---- MENU ----\n"
		"0. Turning off mode\n"
		"1. Normal mode\n"
		"2. Electric power saving mode\n"
		"(Choose 0,1 or 2, others to disconnect): ");
}
void *lobby(void *arg)
{
	while (1)
	{
		str_overwrite_stdout();
		
			char choice = getchar();
			getchar();

			switch (choice)
			{
			case '0':
				printf("TURN OFF\n\n");
				break;
			case '1':
				printf("NORMAL MODE\n\n");
				break;
			case '2':
				printf("ELECTRIC POWER SAVING MODE\n\n");
				break;
			default:
				choice = '3';
				printf("DISCONNECTED\n");
			}
			if (choice == '3')
			{
				printf("er");
				send(client_sock, "kill", 5, 0);
				break;
			}
			send(client_sock, &choice, 1, 0);
		}
	my_handler(2);

	return NULL;
}
int isNumber(char s[])
{
	for (int i = 0; s[i] != '\0'; i++)
	{
		if (isdigit(s[i]) == 0)
			return 0;
	}
	return 1;
}
#define BUFFER_SZ 2048
void recv_msg_handler()
{
	char buff[BUFFER_SZ] = {};
	//menu();
	while (1)
	{
		int bytes_received = recv(client_sock, buff, BUFF_SIZE - 1, 0);
		buff[bytes_received] = '\0';
		if (bytes_received > 0)
		{

			if (strcmp(buff, "ok") == 0)
			{
				str_overwrite_stdout();
				menu();
			}else if (strcmp(buff, "exit") == 0){
              break;
			}else if(isNumber(buff))
				{
					// its an integer
					int re =atoi(buff);
					if (re >= 4500)
					{
						printf("The threshold is exceeded 4500W.Power consumming:%d\n", re);
					}
					if (re >= 5000)
					{
						printf("Over supply,power consumming:%d", re);
					}
					else
					{
						printf(
							"Power Consuming:%d\n", re);
					}
				}
			else
			{
				str_overwrite_stdout();
				printf("%s\n", buff);
				menu();
			}

			//	printf("%s\n", buff);
			
		}
			
	}
}
void trim_lf(char *arr, int length)
{
	for (int i = 0; i < length; i++)
	{
		if (arr[i] == '\n')
		{
			arr[i] = '\0';
			break;
		}
	}
}
int main(int argc, char const *argv[])
{
	if (argc != 3)
	{
		printf("Usage: %s <Server IP> <Port>\n", argv[0]);
		exit(1);
	}

	signal(SIGINT, my_handler);
	char name[50];	 // device name
	int normal_mode; // power for normal mode
	int save_mode;	 // power for saving mode

	printf("Device name: ");
	fgets(name, BUFFER_SZ, stdin);
	trim_lf(name, BUFFER_SZ);
	// scanf("%s",name);
	printf("Normal power mode: ");
	scanf("%d", &normal_mode);
	getchar();
	printf("Limited power mode: ");
	scanf("%d", &save_mode);
	getchar();

	// Step 0: Init variable

	char buff[BUFF_SIZE];
	struct sockaddr_in server_addr;
	int msg_len, bytes_sent, bytes_received;

	// Step 1: Construct socket
	client_sock = socket(AF_INET, SOCK_STREAM, 0);

	// Step 2: Specify server address
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(atoi(argv[2]));
	server_addr.sin_addr.s_addr = inet_addr(argv[1]);

	// Step 3: Request to connect server
	if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) < 0)
	{
		perror("accept() failed\n");
		exit(1);
	}

	// Step 4: Communicate with server

	// First, send device info to server
	memset(buff, '\0', strlen(buff) + 1);
	sprintf(buff, "%s|%d|%d", name, normal_mode, save_mode);
	msg_len = strlen(buff);
	if (msg_len == 0)
	{
		printf("No info on device\n");
		close(client_sock);
		exit(1);
	}
	bytes_sent = send(client_sock, buff, msg_len, 0);
	if (bytes_sent <= 0)
	{
		printf("Connection close\n");
		close(client_sock);
		exit(1);
	}
    
	/*
	if (pthread_create(&lobby_thread, NULL, &lobby, NULL) != 0)
	{
		printf("ERROR: pthread\n");
		return EXIT_FAILURE;
	}

	if (pthread_create(&recv_msg_thread, NULL, (void *)recv_msg_handler, NULL) != 0)
	{
		printf("ERROR: pthread\n");
		return EXIT_FAILURE;
	}  */

	
	// Wait for server response
	if (fork() == 0)
	{
		// Child: listen from server
		// nen work voi child
		while (1)
		{
			bytes_received = recv(client_sock, buff, BUFF_SIZE - 1, 0);
			if (bytes_received <= 0)
			{
				// if DISCONNECT
				printf("\nServer shuted down.\n");
				break;
			}
			else
			{

				int no_devices = atoi(buff); // number of devices connected

				if (no_devices == 9)
				{
					printf("Max devices reached. Can't connect to server\n");
				}
			}
		}
	}
	else
	{
		// Parent: open menu for user
		/*
		if (pthread_create(&lobby_thread, NULL, &lobby, NULL) != 0)
		{
			printf("ERROR: pthread\n");
			return EXIT_FAILURE;
		}

		if (pthread_create(&recv_msg_thread, NULL, (void *)recv_msg_handler, NULL) != 0)
		{
			printf("ERROR: pthread\n");
			return EXIT_FAILURE;
		}  */
       
		do
		{
			sleep(0.5);
			printf(
				"---- MENU ----\n"
				"0. Turning off mode\n"
				"1. Normal mode\n"
				"2. Electric power saving mode\n"
				"(Choose 0,1 or 2, others to disconnect): ");

			char choice = getchar();
			getchar();

			switch (choice)
			{
			case '0':
				printf("TURN OFF\n\n");
				break;
			case '1':
				printf("NORMAL MODE\n\n");
				break;
			case '2':
				printf("ELECTRIC POWER SAVING MODE\n\n");
				break;
			default:
				choice = '3';
				printf("DISCONNECTED\n");
			}
			if (choice == '3')
			{

				send(client_sock, "kill", 5, 0);
				break;
			}
			send(client_sock, &choice, 1, 0);
			// neu nhan ve tin hieu  normal thi continue neu co warning thresh hold thi phai canh bao
			// recv = OK continue ;      else print warning

			bzero(buff, 8192);
			//integer
			int re=2;
			bytes_received = recv(client_sock, &re,sizeof(re), 0);
			if (bytes_received <= 0)
			{
				// if DISCONNECT
				printf("\nServer shuted down.\n");
				continue;
			}
			else
			{
				//buff[bytes_received] = '\0';
				
				//int a = buff[0] - '0';
				printf("Receive:%d\n", re);
				
				if (re >= 4500)
				{
					printf("The threshold is exceeded 4500W.Power consumming:%d\n", re);
				}
				if (re >= 5000)
				{
					printf("Over supply,power consumming:%d", re);
				}
				else
				{
					printf(
						"Power Consuming:%d\n", re);
				} 
			}

		} while (1);  
	} 

	// Step 5: Close socket
	close(client_sock);
	kill(0, SIGKILL);
	return 0;
}