#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <stddef.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include <math.h>

// TODO NEU QUA TAI THI BLOCK CONNECT TOI
#define POWER_THRESHOLD 5000
#define WARNING_THRESHOLD 4500


#define BACKLOG 10 /* Maximum connection */
#define BUFF_SIZE 8192
#define MAX_DEVICE 10
#define MAX_LOG_DEVICE 100
#define MSG_SIZE 1003

#define BUFFER_SZ 2048
#define NAME_LEN 100


 int cli_count = 0;


pthread_t connectMng, powerSupply, elePowerCtrl, powSupplyInfoAccess, logWrite;
int connectMng_t, powerSupply_t, elePowerCtrl_t, powSupplyInfoAccess_t, logWrite_t;
// client structure
pthread_mutex_t device_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t power_mutex = PTHREAD_MUTEX_INITIALIZER;

char use_mode[][10] = {"off", "normal", "saving"};
key_t key_s = 8888, key_d = 1234, key_m = 5678; // system info, device storage, msg_t queue
int  msqid;                    // system info, device storage, msg_t queue
FILE *log_server;
char buffer[2048];
// power system struct
typedef struct
{
    int current_power;
    int threshold_over;
    int supply_over;
    int reset;
} powerSystem_t;
powerSystem_t *powerSystem;

// device struct
typedef struct
{
    int pid;
    char name[50];
    int use_power[3];
    int mode;
    int conn_sock;
} device_t;
device_t *devices;

/**
 * msg_t struct
 * mtype = 1 -> logWrite_handler
 * mtype = 2 -> powSupplyInfoAccess_handler
 */
typedef struct
{
    long mtype;
    char mtext[MSG_SIZE];
} msg_t;

void send_msg_all(char *msg)
{

    for (int no = 0; no < MAX_DEVICE; no++)
    {
        if (devices[no].conn_sock > 0)
        {
            // printf("Connsock:%d\n", devices[no].conn_sock);
            send(devices[no].conn_sock, msg, strlen(msg), 0);
            // write(devices[no].conn_sock, temp, strlen(temp));
        }
    }
}

// write log to server
void printServer(char *content)
{
    char time_log[255];
    time_t t = time(NULL);
    struct tm *now = localtime(&t);
    strftime(time_log, sizeof(time_log), "%Y-%m-%d %H:%M:%S", now);
    char s[1000];
    sprintf(s, "%s : %5ld|%s", time_log, pthread_self(), content);
    printf("%s", s);
}

void sigHandleSIGINT()
{
    msgctl(msqid, IPC_RMID, NULL);
    fclose(log_server);
    kill(0, SIGKILL);
    exit(0);
}

int i = 0;
void *logWrite_handler(void *arg)
{
    // mtype == 1
    msg_t got_msg;

    ///////////////////////////
    // Create sever log file //
    ///////////////////////////
    char file_name[255];
    time_t t = time(NULL);
    struct tm *now = localtime(&t);
    strftime(file_name, sizeof(file_name), "log/server_%Y-%m-%d_%H:%M:%S.txt", now);
    log_server = fopen(file_name, "w");

    bzero(buffer, 2048);
    sprintf(buffer, "Log server started, file is %s\n", file_name);
    printServer(buffer);
    // printServer("Log server started, file is %s\n", file_name);

    ///////////////////////////////
    // Listen to other processes //
    ///////////////////////////////
    while (1)
    {
        // got mail!
        if (msgrcv(msqid, &got_msg, MSG_SIZE, 1, 0) == -1)
        {
            printServer("msgrcv() error");
            exit(1);
        }

        // header = 's' => Write log to server
        if (got_msg.mtext[0] == 's')
        {
            char buff[MSG_SIZE];
            // extract from msg_t
            sscanf(got_msg.mtext, "%*2c%[^|]|", buff);

            char log_time[20]; // current time

            // format the current time
            strftime(log_time, sizeof(log_time), "%Y-%m-%d %H:%M:%S", now);
            // write log
            fprintf(log_server, "%s | %s\n", log_time, buff);
        }
    }
    pthread_detach(pthread_self());
    pthread_exit(NULL);
} // end function logWrite_handler

void *powerSupply_handle(void *arg)
{
    int is_first_msg_t = 1;
    int conn_sock = *((int *)arg);

    char buffer[BUFFER_SZ];
    char command[BUFFER_SZ];

   

    // name nhan tin hieu

    
    while (1)
    {

        int bytes_received = recv(conn_sock, buffer, BUFFER_SZ, 0);

        if (bytes_received <= 0)
        {
            // if DISCONNECT
            // send msg_t to powSupplyInfoAccess
            msg_t sys_msg;
            sys_msg.mtype = 2;
            sprintf(sys_msg.mtext, "d|%ld|", pthread_self()); // n for DISS
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

          
            strcpy(buffer, "kill");
            send(conn_sock, buffer, strlen(buffer), 0);
            pthread_detach(pthread_self());

            break;
        }
        buffer[bytes_received] = '\0';

        if (strcmp(buffer, "kill") == 0)
        {
            // TODO : change getpid thanh pthread_self()
            msg_t sys_msg;
            sys_msg.mtype = 2;
            sprintf(sys_msg.mtext, "d|%ld|", pthread_self()); // n for DISS
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
           
            strcpy(buffer, "kill");
            send(conn_sock, buffer, strlen(buffer), 0);
            pthread_detach(pthread_self());
            // send(conn_sock, "exit", 5, 0);
            break;
        }
        else
        { // TODO: gui du lieu elecpower control

            if (is_first_msg_t)
            {
                is_first_msg_t = 0;
                // send device info to powSupplyInfoAccess
                msg_t sys_msg;
                sys_msg.mtype = 2;
                sprintf(sys_msg.mtext, "n|%ld|%s|%d|", pthread_self(), buffer, conn_sock); // n for NEW
                msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
            }
            else
            {
                // if not first time client send
                // send mode to powSupplyInfoAccess
                msg_t sys_msg;
                sys_msg.mtype = 2;
                sprintf(sys_msg.mtext, "m|%ld|%s|", pthread_self(), buffer); // m for MODE
                msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
            }
        }
        strcpy(buffer, "ok");
        send(conn_sock, buffer, strlen(buffer), 0);
        bzero(buffer, BUFFER_SZ);
    }

    close(conn_sock);
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}
int port;
/* reads and writes powerSupplyEquipInfo or powerSupplySystemInfo
   according to the demand of another process. */
void powSupplyInfoAccess_handler(void *arg)
{
    // mtype = 2
    msg_t got_msg;

    ////////////////
    // check message //
    ////////////////
    while (1)
    {
        // got message!
        if (msgrcv(msqid, &got_msg, MSG_SIZE, 2, 0) <= 0)
        {
            printServer("msgrcv() error");
            exit(1);
        }

        // header = 'n' => Create new device
        if (got_msg.mtext[0] == 'n')
        {
            int no;
            pthread_mutex_lock(&device_mutex);
            for (no = 0; no < MAX_DEVICE; no++)
            {
                if (devices[no].pid == 0)
                    break;
            }
            sscanf(got_msg.mtext, "%*c|%d|%[^|]|%d|%d|%d|",
                   &devices[no].pid,
                   devices[no].name,
                   &devices[no].use_power[1],
                   &devices[no].use_power[2],
                   &devices[no].conn_sock);
            devices[no].mode = 0;
            pthread_mutex_unlock(&device_mutex);

            printServer("--- Connected device info ---\n");
            bzero(buffer, 2048);
            sprintf(buffer, "      Device: %s\n", devices[no].name);
            printServer(buffer);
            bzero(buffer, 2048);
            sprintf(buffer, "      Normal mode: %dW\n", devices[no].use_power[1]);
            printServer(buffer);
            bzero(buffer, 2048);
            sprintf(buffer, "      Saving mode: %dW\n", devices[no].use_power[2]);
            printServer(buffer);
            bzero(buffer, 2048);
            sprintf(buffer, "      Connect socket: %d\n", devices[no].conn_sock);
            printServer(buffer);
            bzero(buffer, 2048);
            sprintf(buffer, "      Used mode: %s\n", use_mode[devices[no].mode]);
            printServer(buffer);

            printServer("-----------------------------\n\n");
            bzero(buffer, 2048);
            sprintf(buffer, "System power using: %dW\n", powerSystem->current_power);
            printServer(buffer);

            // send msg_t to logWrite
            msg_t sys_msg;
            sys_msg.mtype = 1;
            sprintf(sys_msg.mtext, "s|[%s] connected (Normal mode: %dW, Saving mode: %dW)|",
                    devices[no].name,
                    devices[no].use_power[1],
                    devices[no].use_power[2]);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

            sprintf(sys_msg.mtext, "s|Device [%s] set mode to [off] ~ using 0W|", devices[no].name);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
        }

        // header = 'm' => Change the mode!
        if (got_msg.mtext[0] == 'm')
        {
            int no, temp_pid, temp_mode;
            // read ignore first char
            sscanf(got_msg.mtext, "%*c|%d|%d|", &temp_pid, &temp_mode);

            pthread_mutex_lock(&device_mutex);
            for (no = 0; no < MAX_DEVICE; no++)
            {
                if (devices[no].pid == temp_pid)
                    break;
            }
            devices[no].mode = temp_mode;
            pthread_mutex_unlock(&device_mutex);

            // send msg_t to logWrite
            msg_t sys_msg;
            sys_msg.mtype = 1;
            char temp[MSG_SIZE];
            char notify[MSG_SIZE];
            sprintf(temp, "Device [%s] change mode to [%s], comsume %dW\n",
                    devices[no].name,
                    use_mode[devices[no].mode],
                    devices[no].use_power[devices[no].mode]);
            // tODO : SEND TO ALL

            printServer(temp);
            sprintf(sys_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

            // notify change
            sprintf(notify, "You change mode to [%s], comsume %dW\n",

                    use_mode[devices[no].mode],
                    devices[no].use_power[devices[no].mode]);
            send(devices[no].conn_sock, notify, strlen(notify), 0);
            // end notify

            sleep(1);
            sprintf(temp, "System power using: %dW\n", powerSystem->current_power);
            printServer(temp);
            sprintf(sys_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
        }

        // header = 'd' => Disconnect
        if (got_msg.mtext[0] == 'd')
        {
            int no, temp_pid;
            sscanf(got_msg.mtext, "%*c|%d|", &temp_pid);

            // send msg_t to logWrite
            msg_t sys_msg;
            sys_msg.mtype = 1;
            char temp[MSG_SIZE];

            int found = 0;

            pthread_mutex_lock(&device_mutex);
            for (no = 0; no < MAX_DEVICE; no++)
            {
                if (devices[no].pid != 0)
                {
                    if (devices[no].pid == temp_pid)
                    {
                        cli_count = cli_count-1;
                        sprintf(temp, "Device [%s] disconnected\n\n", devices[no].name);
                        printServer(temp);
                        devices[no].pid = 0;
                        strcpy(devices[no].name, "");
                        devices[no].use_power[0] = 0;
                        devices[no].use_power[1] = 0;
                        devices[no].use_power[2] = 0;
                        devices[no].mode = 0;
                        found = 1;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&device_mutex);
            if (found == 0)
            {
                printServer("Error! Device not found\n\n");
            }
            sprintf(sys_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

            int totalPower = 0;

            for (i = 0; i < MAX_DEVICE; i++)
            {
                totalPower += devices[i].use_power[devices[i].mode];
            }
            powerSystem->current_power = totalPower;
            sprintf(temp, "System power using: %dW\n", powerSystem->current_power);
            printServer(temp);
            sprintf(sys_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
        }
    }
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}
double what_time_is_it()
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
}

/*ElePowerCtrl controls limitation/dismantling of control
of the power supply by changing powerSupplySystemInfo.*/

void *elePowerCtrl_handler(void *arg)
{

    int i;
    int warn_threshold = 0;

    while (1)
    {
        // get total power using
        int totalPower = 0;

        for (i = 0; i < MAX_DEVICE; i++)
        {
            totalPower += devices[i].use_power[devices[i].mode];
        }
        powerSystem->current_power = totalPower;

        // check threshold
        if (powerSystem->current_power >= POWER_THRESHOLD)
        { 
            powerSystem->supply_over = 1;
            powerSystem->threshold_over = 1;
        }
        else if (powerSystem->current_power >= WARNING_THRESHOLD)
        {
            powerSystem->supply_over = 0;
            powerSystem->threshold_over = 1;
            powerSystem->reset = 0;
        }
        else
        {
            warn_threshold = 0;
            powerSystem->supply_over = 0;
            powerSystem->threshold_over = 0;
            powerSystem->reset = 0;
        }

        // WARN over threshold
        if (powerSystem->threshold_over && !warn_threshold)
        {
            warn_threshold = 1;

            // send msg_t to logWrite
            msg_t sys_msg;
            sys_msg.mtype = 1;
            char temp[MSG_SIZE];
            sprintf(temp, "WARNING!!! The threshold is exceeded, power comsuming: %dW\n", powerSystem->current_power);
            printServer(temp);
            sprintf(sys_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
            // todo: warning

            for (int no = 0; no < MAX_DEVICE; no++)
            {
                if (devices[no].conn_sock > 0)
                {
                    // printf("Connsock:%d\n", devices[no].conn_sock);
                    send(devices[no].conn_sock, temp, strlen(temp), 0);
                    // write(devices[no].conn_sock, temp, strlen(temp));
                }
            }
        }

        // overload

        if (powerSystem->supply_over)
        {
            // send msg_t to logWrite
            msg_t sys_msg;
            sys_msg.mtype = 1;
            char temp[MSG_SIZE];

            sprintf(temp, "DANGER!!! Supply is exceeded, power comsuming: %dW\n", powerSystem->current_power);
            printServer(temp);
            sprintf(sys_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

            // todo: warning to all

            for (int no = 0; no < MAX_DEVICE; no++)
            {
                if (devices[no].conn_sock > 0)
                {
                    // printf("Connsock:%d\n", devices[no].conn_sock);
                    send(devices[no].conn_sock, temp, strlen(temp), 0);
                }
            }

            // change all to limited
            int no;
            for (no = 0; no < MAX_DEVICE; no++)
            {
                if (devices[no].mode == 1)
                {
                    sys_msg.mtype = 2;
                    sprintf(sys_msg.mtext, "m|%d|2|", devices[no].pid);
                    msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
                }
            }
            sleep(2);

            // TODO : check trong vong 10s
            printServer("Server reset in 10 seconds\n");
            // TODO: send to all
            send_msg_all("You need to change mode to save it.Server reset in 10 seconds\n");
            double start = what_time_is_it();
            double time_taken;
            int flag = 0; // flag == 0 chua xu li dc
            do
            {
                totalPower = 0;
                for (i = 0; i < MAX_DEVICE; i++)
                {
                    totalPower += devices[i].use_power[devices[i].mode];
                }
                powerSystem->current_power = totalPower;

                if (powerSystem->current_power < POWER_THRESHOLD)
                {
                    powerSystem->supply_over = 0;
                    bzero(buffer, 2048);
                    // sprintf(buffer, "Problem has been resolved.OK, power now is %d\n", powerSystem->current_power);
                    sprintf(buffer, "Problem has been resolved\n");
                    // send to all devices
                    send_msg_all(buffer);
                    printServer(buffer);
                    flag = 1;
                    break;
                }

                time_taken = what_time_is_it() - start;
                printf("Time taken : %f\n", time_taken);
                sleep(1);
            } while (time_taken <= 10.0);

            if (flag == 0)
            {
                pid_t my_child;
                if ((my_child = fork()) == 0)
                {
                    // in child

                    int no, count = 0;
                    for (no = 0; no < MAX_DEVICE; no++)
                    {
                        if (devices[no].mode != 0)
                        {
                            count++;
                        }
                    }
                    for (no = count - 1; no >= 0; no--)
                    {
                        // sau 10s ngat thiet bi cuoi cung gay ra qua tai
                        // TODO : ngat thiet bi cuoi cung gay ra qua tai
                        if (devices[no].conn_sock > 0)
                        {
                            if (powerSystem->current_power >= POWER_THRESHOLD)
                            {
                                sys_msg.mtype = 2;
                                sprintf(sys_msg.mtext, "m|%d|0|", devices[no].pid);
                                msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
                                sleep(1);
                            }
                            else
                                break;
                        }
                        // update total power
                        int totalPower = 0;
                        for (int i = 0; i < MAX_DEVICE; i++)
                        {
                            if (devices[no].mode != 0)
                            {
                                totalPower += devices[i].use_power[devices[i].mode];
                            }
                        }
                        powerSystem->current_power = totalPower;
                    }
                    kill(getpid(), SIGKILL);
                }
                else
                {
                    // in parent  sau khi da on dinh
                    while (1)
                    {
                        // todo wait resolve

                        totalPower = 0;
                        for (i = 0; i < MAX_DEVICE; i++)
                            totalPower += devices[i].use_power[devices[i].mode];
                        powerSystem->current_power = totalPower;

                        if (powerSystem->current_power < POWER_THRESHOLD)
                        {
                            powerSystem->supply_over = 0;
                            bzero(buffer, 2048);
                            sprintf(buffer, "OK, power now is %d\n", powerSystem->current_power);
                            // TODO: send to all
                            send_msg_all(buffer);

                            printServer(buffer);
                            // printServer("OK, power now is %d\n", powerSystem->current_power);
                            kill(my_child, SIGKILL);
                            break;
                        }
                    }
                }
            }
        }
    }
    pthread_detach(pthread_self());
    pthread_exit(NULL);
} // endwhile
  // end function elePowerCtrl_handler

void *connectMng_handler(void *arg)
{
    char buffer[2048];
    
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    // Socket settings
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // serv_addr.sin_addr.s_addr = inet_addr("172.17.8.219");
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Error bind\n");
        return 0;
    }

    // listen
    if (listen(listenfd, 10) < 0)
    {
        printf("ERROR: listen\n");
        return 0;
    }

    while (1)
    {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        cli_count=cli_count+1;
        
        if ((cli_count) > MAX_DEVICE)
        {
            printServer("Maximun of clients are connected, Connection rejected");
            strcpy(buffer, "full");
            send(connfd, buffer, strlen(buffer), 0);
            close(connfd);
            continue;
        }
        bzero(buffer, 2048);
        sprintf(buffer, "A device connected, connectMng create new thread powerSupply --- pid: %d.\n", powerSupply_t);
        printServer(buffer);
        pthread_create(&tid, NULL, &powerSupply_handle, (void *)&connfd);

        // reduce CPU usage
        sleep(1);
    }
    close(listenfd);
    pthread_detach(pthread_self());
    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        return 0;
    }

    // char *ip = "127.0.0.1";
    port = atoi(argv[1]);

    powerSystem = (powerSystem_t *)malloc(1 * sizeof(powerSystem_t));
    devices = (device_t *)malloc(10 * sizeof(device_t));
    // Signals
    powerSystem->current_power = 0;
    powerSystem->threshold_over = 0;
    powerSystem->supply_over = 0;
    powerSystem->reset = 0;

    // Init data for shared memory
    int i;
    for (i = 0; i < MAX_DEVICE; i++)
    {
        devices[i].pid = 0;
        strcpy(devices[i].name, "");
        devices[i].use_power[0] = 0;
        devices[i].use_power[1] = 0;
        devices[i].use_power[2] = 0;
        devices[i].mode = 0;
    }

    //////////////////////////////////
    // Create msg_t queue for IPC //
    //////////////////////////////////
    if ((msqid = msgget(key_m, 0644 | IPC_CREAT)) < 0)
    {
        printServer("msgget() failed\n");
        exit(1);
    }

    ///////////////////
    // Handle Ctrl-C //
    ///////////////////
    signal(SIGINT, sigHandleSIGINT);

    
    if (pthread_create(&connectMng, NULL, (void *)connectMng_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return 0;
    }
    if (pthread_create(&elePowerCtrl, NULL, (void *)elePowerCtrl_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return 0;
    }
    if (pthread_create(&powSupplyInfoAccess, NULL, (void *)powSupplyInfoAccess_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return 0;
    }
    if (pthread_create(&logWrite, NULL, (void *)logWrite_handler, NULL) != 0)
    {
        printf("ERROR: pthread\n");
        return 0;
    }

    connectMng_t = pthread_self();
    elePowerCtrl_t = pthread_self();
    powSupplyInfoAccess_t = pthread_self();
    logWrite_t = pthread_self();

    sprintf(buffer, "SERVER create new thread connectMng ------------------ thread: %ld.\n", connectMng);
    printServer(buffer);
    sprintf(buffer, "SERVER create new thread elePowerCtrl ---------------- thread: %ld.\n", elePowerCtrl);
    printServer(buffer);
    sprintf(buffer, "SERVER create new thread powSupplyInfoAccess --------- thread: %ld.\n", powSupplyInfoAccess);
    printServer(buffer);
    sprintf(buffer, "SERVER create new thread logWrite -------------------- thread: %ld.\n\n", logWrite);
    printServer(buffer);

    // wait for threads to finish
    pthread_join(connectMng, NULL);
    pthread_join(elePowerCtrl, NULL);
    pthread_join(powSupplyInfoAccess, NULL);
    pthread_join(logWrite, NULL);
    printServer("SERVER exited\n\n");

    free(powerSystem);
    free(devices);

    return 0;
}
