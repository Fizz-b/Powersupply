// TODO: thieu phan viet log cho moi thiet bi
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
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

#define POWER_THRESHOLD 5000
#define WARNING_THRESHOLD 4500
#define BACKLOG 10 /* Maximum connection */
#define BUFF_SIZE 8192
#define MAX_DEVICE 10
#define MAX_LOG_DEVICE 100
#define MSG_SIZE 1003

////////////////////
// Variables list //
////////////////////
int conn_sock;
int server_port;
pid_t connectMng, powerSupply, elePowerCtrl, powSupplyInfoAccess, logWrite;
int powerSupply_count = 0;
int listen_sock;
char recv_data[BUFF_SIZE];
int bytes_sent, bytes_received;
struct sockaddr_in server;
struct sockaddr_in client;
int sin_size;
char use_mode[][10] = {"off", "normal", "saving"};
key_t key_s = 8888, key_d = 1234, key_m = 5678; // system info, device storage, msg_t queue
int shmid_s, shmid_d, msqid;					// system info, device storage, msg_t queue
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

// write log to server
void printServer(char *content)
{
	char time_log[255];
	time_t t = time(NULL);
	struct tm *now = localtime(&t);
	strftime(time_log, sizeof(time_log), "%Y-%m-%d %H:%M:%S", now);
	char s[1000];
	sprintf(s, "%s : %5d|%s", time_log, getpid(), content);
	printf("%s", s);
}

void sigHandleSIGINT()
{
	msgctl(msqid, IPC_RMID, NULL);
	shmctl(shmid_s, IPC_RMID, NULL);
	shmctl(shmid_d, IPC_RMID, NULL);
	fclose(log_server);
	kill(0, SIGKILL);
	exit(0);
}

// TODO: tk gui tra message neu qua tai neu ok thi ms tiep tuc
/*PowerSupply writes power supply information on the equipment.
			  notifies eleEquip the power supply and the status. */
void powerSupply_handle(int conn_sock)
{
	// check if this is first time client sent
	int is_first_msg_t = 1;
	//////////////////////////////
	// Connect to shared memory //
	//////////////////////////////
	if ((devices = (device_t *)shmat(shmid_d, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	if ((powerSystem = (powerSystem_t *)shmat(shmid_s, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	while (1)
	{
		///////////////////
		// listen on tcp //
		///////////////////
		bytes_received = recv(conn_sock, recv_data, BUFF_SIZE - 1, 0);
		// TO DO: tra ve thong tin cho client

		if (bytes_received <= 0)
		{
			// if DISCONNECT
			// send msg_t to powSupplyInfoAccess
			msg_t sys_msg;
			sys_msg.mtype = 2;
			sprintf(sys_msg.mtext, "d|%d|", getpid()); // n for DISS
			msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

			powerSupply_count = powerSupply_count - 1;
			// send(conn_sock, "exit", 5, 0);
			//  kill this process
			kill(getpid(), SIGKILL);
			break;
		}
		recv_data[bytes_received] = '\0';

		if (strcmp(recv_data, "kill") == 0)
		{
			msg_t sys_msg;
			sys_msg.mtype = 2;
			sprintf(sys_msg.mtext, "d|%d|", getpid()); // n for DISS
			msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
			powerSupply_count = powerSupply_count - 1;
			// kill this process
			kill(getpid(), SIGKILL);
			// send(conn_sock, "exit", 5, 0);
			break;
		}
		else
		{   //TODO: gui du lieu elecpower control
			/*
			int totalPower = 0;
			for (int i = 0; i < MAX_DEVICE; i++)
			{
				totalPower += devices[i].use_power[devices[i].mode];
			}
			printf("Power:%d\n", totalPower);
			int byte_sent = send(conn_sock, &totalPower, sizeof(totalPower), 0);
			if (byte_sent <= 0)
			{
				printf("Error\n");
			}*/
			
			// if receive msg_t from client
			
			if (is_first_msg_t)
			{
				is_first_msg_t = 0;
				// send device info to powSupplyInfoAccess
				msg_t sys_msg;
				sys_msg.mtype = 2;
				sprintf(sys_msg.mtext, "n|%d|%s|%d|", getpid(), recv_data, conn_sock); // n for NEW
				msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

				int totalPower = 0;
				for (int i = 0; i < MAX_DEVICE; i++)
				{
					if (devices[i].pid == getpid())
					{
						totalPower += devices[i].use_power[atoi(recv_data)];
					}
					else
						totalPower += devices[i].use_power[devices[i].mode];
				}
				printf("Power:%d\n", totalPower);
				int byte_sent = send(conn_sock, &totalPower, sizeof(totalPower), 0);
				if (byte_sent <= 0)
				{
					printf("Error\n");
				}
			}
			else
			{
				// if not first time client send
				// send mode to powSupplyInfoAccess
				msg_t sys_msg;
				sys_msg.mtype = 2;
				sprintf(sys_msg.mtext, "m|%d|%s|", getpid(), recv_data); // m for MODE
				msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

				int totalPower = 0;
				for (int i = 0; i < MAX_DEVICE; i++)
				{
					if(devices[i].pid==getpid()){
                        totalPower += devices[i].use_power[atoi(recv_data)];
					}else totalPower += devices[i].use_power[devices[i].mode];
				}
				printf("Power:%d\n", totalPower);
				int byte_sent = send(conn_sock, &totalPower, sizeof(totalPower), 0);
				if (byte_sent <= 0)
				{
					printf("Error\n");
				}

			}
		}
		printf("Chao nhe\n");
		//send(conn_sock, "ok", 3, 0);
		/*
	   int f=0;
	   msg_t got_msg;

	   if (msgrcv(msqid, &got_msg, MSG_SIZE, 1, 0) == -1)
	   {
		   printServer("msgrcv() error");
		   exit(1);
	   }

	   // header = 's' => Write log to server
	   if (got_msg.mtext[0] == 'p')
	   {
		   char buff[MSG_SIZE];
		   // extract from msg_t
		   sscanf(got_msg.mtext, "%*c|%s|", buff);
		   send(conn_sock, buff, strlen(buff), 0);
		   f=1;
	   }*/

	   
	   //sleep(2);
		

	} // endwhile
} // end function powerSupply_handle

void connectMng_handler()
{
   
	/////////// ////////////
	// Connect to client //
	///////////////////////
	// Step 1: Construct a TCP socket to listen connection request
	if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		printServer("socket() failed\n");
		exit(1);
	}

	// Step 2: Bind address to socket
	bzero(&server, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(server_port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listen_sock, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		printServer("bind() failed\n");
		exit(1);
	}

	// Step 3: Listen request from client
	if (listen(listen_sock, BACKLOG) == -1)
	{
		printServer("listen() failed\n");
		exit(1);
	}

	// Step 4: Communicate with client
	while (1)
	{
		// accept request
		sin_size = sizeof(struct sockaddr_in);
		if ((conn_sock = accept(listen_sock, (struct sockaddr *)&client, &sin_size)) == -1)
		{
			printServer("accept() failed\n");
			continue;
		}
         printf("New connection:%d\n",conn_sock);
		// if 11-th device connect to SERVER
		if (powerSupply_count == MAX_DEVICE)
		{
			char re = '9';
			if ((bytes_sent = send(conn_sock, &re, 1, 0)) <= 0)
				printServer("send() failed\n");
			close(conn_sock);
			break;
		}

		// create new process powerSupply
		if ((powerSupply = fork()) < 0)
		{
			printServer("powerSupply fork() failed\n");
			continue;
		}

		if (powerSupply == 0)
		{
			// in child
			close(listen_sock);
			powerSupply_handle(conn_sock);
			close(conn_sock);
		}
		else
		{
			// in parent
			close(conn_sock);
			powerSupply_count++;
			bzero(buffer, 2048);
			sprintf(buffer, "A device connected, connectMng forked new process powerSupply --- pid: %d.\n", powerSupply);
			printServer(buffer);
		}
	} // end communication

	close(listen_sock);
} // end function connectMng_handler



/* reads and writes powerSupplyEquipInfo or powerSupplySystemInfo
   according to the demand of another process. */
void powSupplyInfoAccess_handler()
{
	// mtype = 2
	msg_t got_msg;

	//////////////////////////////
	// Connect to shared memory //
	//////////////////////////////
	if ((devices = (device_t *)shmat(shmid_d, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	if ((powerSystem = (powerSystem_t *)shmat(shmid_s, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	////////////////
	// check mail //
	////////////////
	while (1)
	{
		// got mail!
		if (msgrcv(msqid, &got_msg, MSG_SIZE, 2, 0) <= 0)
		{
			printServer("msgrcv() error");
			exit(1);
		}

		// header = 'n' => Create new device
		if (got_msg.mtext[0] == 'n')
		{
			int no;
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

			for (no = 0; no < MAX_DEVICE; no++)
			{
				if (devices[no].pid == temp_pid)
					break;
			}
			devices[no].mode = temp_mode;

			// send msg_t to logWrite
			msg_t sys_msg;
			sys_msg.mtype = 1;
			char temp[MSG_SIZE];

			sprintf(temp, "Device [%s] change mode to [%s], comsume %dW\n",
					devices[no].name,
					use_mode[devices[no].mode],
					devices[no].use_power[devices[no].mode]);
			printServer(temp);
			sprintf(sys_msg.mtext, "s|%s", temp);
			msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

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
			for (no = 0; no < MAX_DEVICE; no++)
			{
				if (devices[no].pid != 0)
				{
					if (devices[no].pid == temp_pid)
					{
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
			if (found == 0)
			{
				printServer("Error! Device not found\n\n");
			}
			sprintf(sys_msg.mtext, "s|%s", temp);
			msgsnd(msqid, &sys_msg, MSG_SIZE, 0);

			sprintf(temp, "System power using: %dW\n", powerSystem->current_power);
			printServer(temp);
			sprintf(sys_msg.mtext, "s|%s", temp);
			msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
		}
	}
}
double what_time_is_it()
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	return now.tv_sec + now.tv_nsec * 1e-9;
}
/*ElePowerCtrl controls limitation/dismantling of control
of the power supply by changing powerSupplySystemInfo.*/
void elePowerCtrl_handler()
{
	//////////////////////////////
	// Connect to shared memory //
	//////////////////////////////
	if ((devices = (device_t *)shmat(shmid_d, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	if ((powerSystem = (powerSystem_t *)shmat(shmid_s, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	int i;
	int warn_threshold = 0;
    
	while (1)
	{
		// get total power using
		int totalPower = 0;
		for (i = 0; i < MAX_DEVICE; i++){
			totalPower += devices[i].use_power[devices[i].mode];
		}
		powerSystem->current_power = totalPower;

		char string[20];
		sprintf(string, "%d", totalPower);
		for (int i = 0; i < MAX_DEVICE; i++)
		{
			send(devices[i].conn_sock, string, strlen(string), 0);
		}

		// check threshold
		if (powerSystem->current_power >= POWER_THRESHOLD)
		{ // qua nguong chiu tai
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
             
			 /*
			
			sprintf(temp, "WARNING!!! Over threshold, power comsuming: %dW\n", powerSystem->current_power);
			sprintf(sys_msg.mtext, "p|%s|", temp);
			msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
			*/
             
			 /*
			for (int no = 0; no < MAX_DEVICE; no++)
			{
				if (devices[no].conn_sock != 0)
				{
					printf("Connsock:%d\n", devices[no].conn_sock);
					//send(devices[no].conn_sock, temp, strlen(temp), 0);
					write(devices[no].conn_sock, temp, strlen(temp));
					// phai gui lai cho connectMng de xu ly
				}
			} */
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
			/*
			for (int no = 0; no < MAX_DEVICE; no++)
			{
				if (devices[no].conn_sock != 0)
				{
					printf("Connsock:%d\n", devices[no].conn_sock);
					send(devices[no].conn_sock, temp, strlen(temp), 0);
					// write(devices[no].conn_sock, temp, strlen(temp));
				}
			}  */

			
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
					sprintf(buffer, "OK, power now is %d\n", powerSystem->current_power);
					printServer(buffer);
					// printServer("OK, power now is %d\n", powerSystem->current_power);
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
					sleep(5);

					int no;
					for (no = MAX_DEVICE; no >= 0; no--)
					{
						// sau 10s ngat thiet bi cuoi cung gay ra qua tai
						// TODO : ngat thiet bi cuoi cung gay ra qua tai
						if (devices[no].mode != 0)
						{
							if (powerSystem->current_power >= POWER_THRESHOLD)
							{
								sys_msg.mtype = 2;
								sprintf(sys_msg.mtext, "m|%d|0|", devices[no].pid);
								msgsnd(msqid, &sys_msg, MSG_SIZE, 0);
								sleep(1);

								// update total power
								int totalPower = 0;
								for (int i = 0; i < MAX_DEVICE; i++)
								{
									totalPower += devices[i].use_power[devices[i].mode];
								}
								powerSystem->current_power = totalPower;
							} else break;
						}
					}
					kill(getpid(), SIGKILL);
				}
				else
				{
					// in parent  sau khi da on dinh
					while (1)
					{
						totalPower = 0;
						for (i = 0; i < MAX_DEVICE; i++)
							totalPower += devices[i].use_power[devices[i].mode];
						powerSystem->current_power = totalPower;

						if (powerSystem->current_power < POWER_THRESHOLD)
						{
							powerSystem->supply_over = 0;
							bzero(buffer, 2048);
							sprintf(buffer, "OK, power now is %d\n", powerSystem->current_power);
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

	// todo:send msg_t
	/*
	for (int no = 0; no < MAX_DEVICE; no++)
	{
		if (devices[no].conn_sock != 0)
		{
			send(devices[no].conn_sock, "it's ok", 1003, 0);
		}
	}*/
} // endwhile
  // end function elePowerCtrl_handler

void logWrite_handler()
{
	// mtype == 1
	msg_t got_msg;

	//////////////////////////////
	// Connect to shared memory //
	//////////////////////////////
	/*
	if ((devices = (device_t *)shmat(shmid_d, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

	if ((powerSystem = (powerSystem_t *)shmat(shmid_s, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}*/

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
} // end function logWrite_handler

int main(int argc, char const *argv[])
{
	if (argc != 2)
	{
		printf("Usage:  %s <Server Port>\n", argv[0]);
		exit(1);
	}
	server_port = atoi(argv[1]);
	printf("SERVER start, PID is %d.\n", getpid());

	///////////////////////////////////////////
	// Create shared memory for power system //
	///////////////////////////////////////////
	if ((shmid_s = shmget(key_s, sizeof(powerSystem_t), 0644 | IPC_CREAT)) < 0)
	{
		printServer("shmget() failed\n");
		exit(1);
	}
	if ((powerSystem = (powerSystem_t *)shmat(shmid_s, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}
	powerSystem->current_power = 0;
	powerSystem->threshold_over = 0;
	powerSystem->supply_over = 0;
	powerSystem->reset = 0;

	/////////////////////////////////////////////
	// Create shared memory for devices storage//
	/////////////////////////////////////////////
	if ((shmid_d = shmget(key_d, sizeof(device_t) * MAX_DEVICE, 0644 | IPC_CREAT)) < 0)
	{
		printServer("shmget() failed\n");
		exit(1);
	}
	if ((devices = (device_t *)shmat(shmid_d, (void *)0, 0)) == (void *)-1)
	{
		printServer("shmat() failed\n");
		exit(1);
	}

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

	///////////////////////////////////
	// start child process in SERVER //
	///////////////////////////////////
	if ((connectMng = fork()) == 0)
	{
		connectMng_handler();
	}
	else if ((elePowerCtrl = fork()) == 0)
	{
		elePowerCtrl_handler();
	}
	else if ((powSupplyInfoAccess = fork()) == 0)
	{
		powSupplyInfoAccess_handler();
	}
	else if ((logWrite = fork()) == 0)
	{
		logWrite_handler();
	}
	else
	{
		sprintf(buffer, "SERVER forked new process connectMng ------------------ pid: %d.\n", connectMng);
		printServer(buffer);
		sprintf(buffer, "SERVER forked new process elePowerCtrl ---------------- pid: %d.\n", elePowerCtrl);
		printServer(buffer);
		sprintf(buffer, "SERVER forked new process powSupplyInfoAccess --------- pid: %d.\n", powSupplyInfoAccess);
		printServer(buffer);
		sprintf(buffer, "SERVER forked new process logWrite -------------------- pid: %d.\n\n", logWrite);
		printServer(buffer);
		waitpid(connectMng, NULL, 0);
		waitpid(elePowerCtrl, NULL, 0);
		waitpid(powSupplyInfoAccess, NULL, 0);
		waitpid(logWrite, NULL, 0);
		printServer("SERVER exited\n\n");
	}

	return 0;
}
