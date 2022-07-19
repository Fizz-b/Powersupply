#include <stdio.h>
#include <stdlib.h>
#define BUFF_SIZE 8192
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#define KEY 0XAED
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>
#include <sys/types.h>


#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define NAME_LEN 32

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[NAME_LEN]; // playerName
int player = 1;

pthread_t lobby_thread;     // send message thread
pthread_t recv_msg_thread;  // recv msg thread


char ip[1000];
int port;
void startScreen();
void str_overwrite_stdout()
{
    printf("\r%s", "> ");
    fflush(stdout);
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

void flashScreen()
{
    system("clear");
}

void my_handler(int s)
{

    flag=1;
    send(sockfd, "kill", 5, 0);
    printf("Caught end signal: %d\n", s);
    close(sockfd);
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
        "(Choose 0,1 or 2, others to disconnect):\n");
        
}







void *lobby(void *arg)
{
    char buffer[BUFFER_SZ] = {};
    char uname[100], pass[100];
    while (1)
    {
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
            send(sockfd, "kill", 5, 0);
            break;
        }
        send(sockfd, &choice, 1, 0);
    }

    my_handler(2);

    return NULL;
}


void  *recv_msg_handler(void *arg)
{
  
    char rep[BUFFER_SZ] = {};
    
    // response res;
    flashScreen();

    while (1)
    {

        int receive = recv(sockfd, rep, BUFFER_SZ, 0);
       
        if (receive > 0)
        {
            rep[receive]='\0';
            if(strcmp(rep,"ok")==0){
                menu();
                str_overwrite_stdout();
            }
            else
                if(strcmp(rep, "kill") == 0)
                {
                    break;
                }
                    else
                    {
                        printf("%s\n", rep);
                        menu();
                        str_overwrite_stdout();
                    }
                    // printf("%s---%s", status,message);
                }

            bzero(rep, BUFFER_SZ);
            
        }
    
}

int conectGame(char *ip, int port)
{
    char buff[BUFF_SIZE];
    signal(SIGINT, my_handler);
    char name[50];   // device name
    int normal_mode; // power for normal mode
    int save_mode;   // power for saving mode
   

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

    struct sockaddr_in server_addr;

    // socket settings
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // connect to the server
    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1)
    {
        printf("ERROR: connect\n");
        return EXIT_FAILURE;
    }

    // send the name
    memset(buff, '\0', strlen(buff) + 1);
    sprintf(buff, "%s|%d|%d", name, normal_mode, save_mode);
    int msg_len = strlen(buff);
    if (msg_len == 0)
    {
        printf("No info on device\n");
        close(sockfd);
        exit(1);
    }
   int  bytes_sent = send(sockfd, buff, msg_len, 0);
    if (bytes_sent <= 0)
    {
        printf("Connection close\n");
        close(sockfd);
        exit(1);
    }

    if (pthread_create(&lobby_thread, NULL, &lobby, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }

    if (pthread_create(&recv_msg_thread, NULL, &recv_msg_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return EXIT_FAILURE;
    }
  
    while (1)
    {
        if (flag)
        {
            printf("\nBye\n");
            break;
        }
    }

    close(sockfd);

    return 0;
}




int main(int argc, char **argv)
{
    if (argc != 3)
    {
        printf("error, too many or too few arguments\n");
        printf("Correct format is ./client <ip> <port>");
        return 1;
    }
    //
    port = atoi(argv[2]);
    strcpy(ip, argv[1]);
    conectGame(ip, port);

        return 0;
}
