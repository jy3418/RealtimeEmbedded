#include <stdio.h>
#include <unistd.h>
#include <mraa/aio.h>
#include <sys/time.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <time.h>
#include <mraa.h>
#include <mraa/gpio.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>

//Required global variables
sig_atomic_t volatile run_flag = 1;
const int B = 4275;
time_t timer;
char timebuf[9];
struct tm* timeinfo;
double rawTemp;
double temperature;
mraa_aio_context tempSens;
char scale = 'F';
unsigned int sleepTime = 1;
int isLogging = 0;
int isReporting = 1;
FILE* outputfile;
pthread_mutex_t mlock;
int sockfd;
int portno = -1;
char* id = NULL;
char* hostname = NULL;
struct sockaddr_in serveraddr;
struct hostent *server;

void reportTemp() {
	while (run_flag)
	{
		if (! isReporting) {
			sleep(sleepTime);
			continue;
		}
		char servbuf[100];
		
		rawTemp = mraa_aio_read(tempSens);
			
		double R = (1023.0/((double) rawTemp) - 1.0) * 100000.0;
		double celsius = 1.0 / (log(R/100000.0)/B + 1/298.15) - 273.15;
		double fahrenheit = celsius * 9.0/5.0 + 32;
		
		if (scale == 'F')
			temperature = fahrenheit;
		else if (scale == 'C')
			temperature = celsius;
		
		//Lock mutex for critical condition
		pthread_mutex_lock(&mlock);
		
		time(&timer);
		timeinfo = localtime(&timer);
		strftime(timebuf, 9, "%H:%M:%S", timeinfo);
		
		snprintf(servbuf, 100, "%s %.1f\n", timebuf, temperature);
		
		fprintf(outputfile, "%s %.1f\n", timebuf, temperature);
		
		if (write(sockfd, servbuf, strlen(servbuf)) < 0) {
			fprintf(stderr, "Error: could not write to socket.\n");
			exit(1);
		}
		
		//Unlock mutex
		pthread_mutex_unlock(&mlock);
		
		fflush(stdout);
		sleep(sleepTime);
	}
}

void do_when_interrupted() {
	char servbuf[100];
	
	pthread_mutex_lock(&mlock);
	
	time(&timer);
	timeinfo = localtime(&timer);
	strftime(timebuf, 9, "%H:%M:%S", timeinfo);
	
	snprintf(servbuf, 100, "%s SHUTDOWN\n", timebuf);
	
	fprintf(outputfile, "%s SHUTDOWN\n", timebuf);
	
	if (write(sockfd, servbuf, strlen(servbuf)) < 0) {
			fprintf(stderr, "Error: could not write to socket.\n");
			exit(1);
	}
	pthread_mutex_unlock(&mlock);
	
	fflush(stdout);
	run_flag = 0;
	mraa_aio_close(tempSens);
	close(sockfd);
	exit(0);
}

void processcommand(char* command) {
	size_t length = strlen(command);
	
	if (length == 3 && strcmp(command, "OFF") == 0) {
		pthread_mutex_lock(&mlock);
		fprintf(outputfile, "%s\n", command);
		pthread_mutex_unlock(&mlock);
		do_when_interrupted();
	}
	else if (length == 4 && strncmp(command, "STOP", 4) == 0)
		isReporting = 0;
	else if (length == 5 && strncmp(command, "START", 5) == 0)
		isReporting = 1;
	else if (length == 7 && strncmp(command, "SCALE=", 6) == 0) {
		char scalechar = command[6];
		if (scalechar == 'C' || scalechar == 'F')
			scale = scalechar;
	}
	else if (strncmp(command, "PERIOD=", 7) == 0) {
		char* numbers = command + 7;
		int period = atoi(numbers);
		sleepTime = period;
	}
	
	pthread_mutex_lock(&mlock);
	fprintf(outputfile, "%s\n", command);
	pthread_mutex_unlock(&mlock);
}

int main(int argc, char* argv[])
{	
	//Process command line parameters
	static struct option long_options[] = {
		{"period", required_argument, 0, 'p'},
		{"scale",  required_argument, 0, 's'},
		{"log",    required_argument, 0, 'l'},
		{"id",     required_argument, 0, 'i'},
		{"host",   required_argument, 0, 'h'},
		{0,        0,                 0,   0}
	};
	
	while (1) {
		int option_index = 0;
  
		int c = getopt_long(argc, argv, "", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 'p':
				if (atoi(optarg) > 0)
					sleepTime = atoi(optarg);
				else {
					fprintf(stderr, "Error: period cannot be less than 1\n");
					exit(1);
				}
				break;
			
			case 's':
				;
				char scalec = optarg[0];
				if (scalec != 'F' && scalec != 'C') {
					fprintf(stderr, "Error: unknown scale of temperature.\n");
					exit(1);
				}
				scale = scalec;
				break;
				
			case 'l':
				if ((outputfile = fopen(optarg, "a")) == NULL) {
					fprintf(stderr, "Error opening file for logging.\n");
					exit(1);
				}
				isLogging = 1;
				break;
				
			case 'i':
				;
				char* idnum = optarg;
				if (strlen(idnum) != 9) {
					fprintf(stderr, "Error: id must be 9 digit numbers.\n");
					exit(1);
				}
				id = malloc(14);
				strcpy(id, "ID=");
				strcat(id, idnum);
				strcat(id, "\n");
				break;
			
			case 'h':
				hostname = optarg;
				break;
				
			case '?':
				//Unknown option
				fprintf(stderr, "Usage:%s [--period=..] [--scale=..]\n", argv[0]);
				exit(1);
				break;
		}
	}
	
	//Get port number
	if (optind < argc) {
		portno = atoi(argv[optind]);
		if (portno <= 1024) {
			fprintf(stderr, "Error: Port number must be > 1024.\n");
			exit(1);
		}
	}
	
	//Check if any mandatory parameters are missin
	if (! isLogging || portno == -1 || id == NULL || hostname == NULL) {
		fprintf(stderr, "Error: Missing required parameters: --id=... --host=... --log=... port number\n");
		exit(1);
	}
	
	//Initialize the temperature sensor
	mraa_init();
	tempSens = mraa_aio_init(1);
	
	//Initialize mutex lock
	pthread_mutex_init(&mlock, NULL);
	
	//Connect to server
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		fprintf(stderr, "Error opening socket.\n");
		exit(1);
	}
	
	server = gethostbyname(hostname);
	if (server == NULL) {
		fprintf(stderr, "Error connecting to server %s\n", hostname);
		exit(1);
	}
	
	memset((char*) &serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	memcpy((char*) &serveraddr.sin_addr.s_addr, (char*) server->h_addr, server->h_length);
	serveraddr.sin_port = htons(portno);
	
	if (connect(sockfd, (struct sockaddr*) &serveraddr, sizeof(serveraddr)) < 0) {
		fprintf(stderr, "Error: could not establish connection.\n");
		exit(1);
	}
	
	
	//Write ID to server
	if (write(sockfd, id, strlen(id)) < 0) {
		fprintf(stderr, "Error: could not write to socket.\n");
		exit(1);
	}
	
	//Write ID to file
	fprintf(outputfile, "%s", id);
	
	//Create a thread to process temperature
	pthread_t tempthread;
	if (pthread_create(&tempthread, NULL, (void*) reportTemp, NULL) < 0) {
		fprintf(stderr, "Error creating a new thread.\n");
		exit(1);
	}
	
	//Take commands from server
	while(1) {
		char input[1000] = {0};
		
		if (read(sockfd, input, sizeof(char) * 1000) > 0) {
			//Tokenize and process the command
			char* token = strtok(input, "\n");
			while (token != NULL) {
				processcommand(token);
				token = strtok(NULL, "\n");
			}
		}
	}
	
	mraa_aio_close(tempSens);
	return 0;
}