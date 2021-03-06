/*
Tuyen Pham
TUID 915591991
CIS 3207 - Section 4
11/04/2019
Project 3
This project is to build the network spell checker
 */

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h> // for working with threads, mutexes, and condition variables
#include <string.h> // used for comparing args at start ie. checking for port number vs. dictionary file
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> // need for sockaddr_in struct to use with accept()
#include <unistd.h>
#include "server.h"

/*Define variables*/
pthread_t threadPool[NUM_WORKER_THREADS], logThread; // declare global thread pool to use as worker threads
pthread_mutex_t job_mutex, log_mutex; // declare mutexes to use for job and log buffers
pthread_cond_t job_cv_cs, job_cv_pd; // delcare condition variables for job buffer
pthread_cond_t log_cv_cs, log_cv_pd; // delcare condition varialbes for log buffer
int jobBuff[JOB_BUF_LEN]; // array of ints that represent the socket for a client trying to connect
char logBuff[LOG_BUF_LEN][PHRASE_SIZE]; // array of phrases to be written to log file
int jobLen = JOB_BUF_LEN; // capacity of job buffer
int logLen = LOG_BUF_LEN; // capacity of log buffer
int jobCount, logCount = 0; // number of items in each buffer
int jobFront = -1; // used to keep track of front of job buffer
int jobRear = 0; // used to keep track of rear of job buffer
int logFront = -1; // used to keep track of front of log buffer
int logRear = 0; // used to keep track of rear of log buffer

int connectionPort = 0; // declare global connectionPort to be used
char* dictionaryName = ""; // declare global dictionaryName to be used
char dictionary[DICTIONARY_SIZE][MAX_WORD_SIZE]; // dictionary to store words from dictionaryName file in
int wordsInDictionary = 0; // global var to keep track of word count of dictionary - to be used when searching

struct sockaddr_in client;
int clientLen = sizeof(client);
int connectionSocket, clientSocket;

char* clientMessage = "Hello! You're connected to the server. Send the server a word to spell check!\n";
char* msgRequest = "Send me another word to spell check! Or, enter the escape key and hit enter to quit this connection..\n";
char* msgClose = "Goodbye!\n";
FILE* logFile_ptr; // log file pointer to open log file with

int main(int argc, char** argv) {
	/* Initialize mutexes*/

  // check that job mutex initialized
	if(pthread_mutex_init(&job_mutex, NULL) != 0) { 
		printf("Error initializing job mutex!\n");
		return -1;
	}
  // check that log mutex initialized
	if(pthread_mutex_init(&log_mutex, NULL) != 0) {
		printf("Error initializing log mutex!\n");
		return -1;
	}
	/* Initialize condition variables*/

  // check that job buffer consume condition variable initialized
	if (pthread_cond_init(&job_cv_cs, NULL) != 0) { 
		printf("Error initializing job buffer consume condition variable!\n");
		return -1;
	}
  // check that job buffer consume condition variable initialized
	if (pthread_cond_init(&job_cv_pd, NULL) != 0) { 
		printf("Error initializing job buffer produce condition variable!\n");
		return -1;
	}
  // check that job buffer consume condition variable initialized
	if (pthread_cond_init(&log_cv_cs, NULL) != 0) {
		printf("Error initializing log buffer consume condition variable!\n");
		return -1;
	}
  // check that job buffer consume condition variable initialized
	if (pthread_cond_init(&log_cv_pd, NULL) != 0) { 
		printf("Error initializing log buffer produce condition variable!\n");
		return -1;
	}

	// Create threads - worker threads and log thread
	for (int i = 0; i < NUM_WORKER_THREADS; i++) {
		if(pthread_create(&threadPool[i], NULL, workerThreadFunc, NULL) == 0) { // pthread_create() will return 0 on success
			#ifdef TESTING
			printf("Worker thread created.\n");
			#endif
		}
	}
  // create log thread to write phrases from log buffer to log file - // pthread_create() will return 0 on success
	if (pthread_create(&logThread, NULL, logThreadFunc, NULL) == 0) { 
		#ifdef TESTING
		printf("Log thread created.\n");
		#endif
	}

	// If no port or dictionaryName is specified by user, use the default port and default dictionaryName
	if (argc == 1){
		printf("No port number entered. Plese use the default port 7000!\n");
		// use DEFAULT_PORT and DEFAULT_DICTIONARY
		connectionPort = DEFAULT_PORT;
		dictionaryName = DEFAULT_DICTIONARY;
		#ifdef TESTING
		printf("THE DICTIONARY FILE IS: %s\n", dictionaryName); // FOR TESTING
		#endif
	} else if (argc == 2) { // check whether second argument is a port number or a dictionaryName file
		// If it is just a port number, be sure to make dictionaryName the DEFAULT_DICTIONARY
		if (strstr(argv[1], ".txt") == NULL){ // if the second arg does not contain .txt, it's safe to assume it's a port number
			connectionPort = atoi(argv[1]); // set connectionPort to second arg
			dictionaryName = DEFAULT_DICTIONARY; // set dictionaryName to DEFAULT_DICTIONARY since no dictionaryName was specified
			#ifdef TESTING
			printf("Assigned dictionaryName to DEFAULT_DICTIONARY!\n"); // FOR TESTING
			#endif
		} 
		// If it is just a dictionaryName, be sure to make connectionPort the DEFAULT_PORT
		if (strstr(argv[1], ".txt") != NULL) { // if the second arg contains .txt, it's safe to assume it's a dictionaryName file
			connectionPort = DEFAULT_PORT; // set connectionPort to DEFAULT_PORT since no port was specified
			dictionaryName = argv[1]; // set dictionaryName to user-specified dictionaryName file name
			#ifdef TESTING
			printf("Assigned connectionPort to DEFAULT_PORT!\n"); // FOR TESTING
			#endif
		}
		printf("You entered 2 arguments! Where port number is %d and dictionary is %s\n", connectionPort, dictionaryName); // FOR TESTING
	} else if (argc == 3) { // check whether second arg is a port number and third arg is a dictionaryName OR second arg is a dictionaryName and third arg is a port number
		// If the second arg is a port number AND the third arg is a dictionaryName file, assign those args to the connectionPort and dictionaryName global variables respectively
		if ((strstr(argv[1], ".txt") == NULL) && (strstr(argv[2], ".txt") != NULL)) {
			#ifdef TESTING
			printf("Second arg was: %s\n", argv[1]); // FOR TESTING
			printf("Third arg was: %s\n", argv[2]); // FOR TESTING
			#endif
			connectionPort = atoi(argv[1]); // set connectionPort to user-specified port
			dictionaryName = argv[2]; // set dictionaryName to user-specified dictionaryName
		// Else-If the second arg is a dictionaryName AND the third arg is a port number, assign those args to the dictionaryName and connectionPort global variables respectively
		} else if ((strstr(argv[1], ".txt") != NULL) && (strstr(argv[2], ".txt") == NULL)) {
			#ifdef TESTING
			printf("Second arg was: %s\n", argv[1]); // FOR TESTING
			printf("Third arg was: %s\n", argv[2]); // FOR TESTING
			#endif
			dictionaryName = argv[1]; // set dictionaryName to user-specified dictionaryName
			connectionPort = atoi(argv[2]); // set connectionPort to user-specified port
		} else {
			printf("Please enter an appropriate command.\nFor example: './server', './server PORTNUMBER', './server DICTIONARYFILE', './server PORTNUMBER DICTIONARYFILE', './server DICTIONARYFILE PORTNUMBER'\n");
			return -1;
		}
    // FOR TESTING
		printf("You entered 3 arguments! Where port number is %d and dictionary is %s\n", connectionPort, dictionaryName); 
	} else { 
    // otherwise too many arguments were entered, print error message asking user to enter proper number of args and return -1
		printf("Please enter an appropriate command.\nFor example: './server', './server PORTNUMBER', './server DICTIONARYFILE', './server PORTNUMBER DICTIONARYFILE', './server DICTIONARYFILE PORTNUMBER'\n");
		return -1;
	}

	// Attempt to open dictionaryName file to load words into dictionary
	FILE* dictionaryName_ptr = fopen(dictionaryName, "r"); // open dictionary file for reading
	if (dictionaryName_ptr == NULL) { // if dictionaryName_ptr is NULL, there was an issue opening the file
		printf("Error opening dictionary file!\n"); // print an error
		return -1; // return -1 to exit program
	} else { // otherwise dictionary file was opened for reading successfully
		// Store words from dictionary file into dictionary[][]
		int i = 0;
    // store the word from the dictionary into dictionary data structure (two-dimensional char array in our case) fgets() also takes care of appending '\0' to the end of the word :) - using a while with fgets() like this ensures we don't read past the end of the statically allocated dictionary[][]
		while((fgets(dictionary[i], sizeof(dictionary[i]), dictionaryName_ptr) != NULL) && (i < (DICTIONARY_SIZE - 1))) {  
			wordsInDictionary++;
			#ifdef TESTINGDICTIONARY
			printf("WORD: %s", dictionary[i]); // print each word read into dictionary from dictionary FOR TESTING
			#endif
			i++;
		}
		#ifdef TESTINGDICTIONARY
		printf("Word count of words in the dictionary: %d\n", wordsInDictionary); // FOR TESTING
		#endif
		fclose(dictionaryName_ptr); // close dictionary file
	}

	// We can't use ports below 1024 and ports above 65535 don't exist.
	if (connectionPort < 1024 || connectionPort > 65535){
		printf("Port number is either too low(below 1024), or too high(above 65535).\n");
		return -1;
	}
	printf("Waiting to make a connection.. :)\n");

	// Use open_listenfd in server.h (from Ch. 11 O'Halloran) to get socket descriptor to listen for incoming connections
	connectionSocket = open_listenfd(connectionPort);
	if (connectionSocket == -1){ // if connectionSocket is -1
		printf("Could not connect to %s, maybe try another port number?\n", argv[1]); // print an error
		return -1; // return -1 to end program
	}

	// Continously accept connections to process once currently connected client quits so long as a worker thread is available for servicing
	while(1) { 
		/* accept() waits until a user connects to the server, writing information about that server into the sockaddr_in client.
		If the connection is successful, we obtain A SECOND socket descriptor. 
		There are two socket descriptors being used now:
		One by the server to listen for incoming connections.
		The second that was just created that will be used to communicate with the connected user. */
		if ((clientSocket = accept(connectionSocket, (struct sockaddr*)&client, &clientLen)) == -1){
			printf("Error connecting to client.\n");
			return -1;
		}

		// Add clientSocket to job buffer inside job mutex
		pthread_mutex_lock(&job_mutex); // lock mutex for job buffer
		while(jobCount == JOB_BUF_LEN) {// while job buffer is full
			pthread_cond_wait(&job_cv_pd, &job_mutex); // wait for job buffer to NOT be full
		}

		// Insert clientSocket into jobBuffer
		if (jobCount == JOB_BUF_LEN) { // if the job buffer is full
			printf("Job buffer is FULL!\n"); // print that it's full
		} else {
      // if the job buffer is empty, make the item the first in the queue, update front and rear
			if (jobCount == 0) {
				jobFront = 0;
				jobRear = 0;
			} 
			jobBuff[jobRear] = clientSocket; // add the clientSocket to the job buffer
			jobCount++; // increment count of socket descriptors in the job buffer
			#ifdef TESTING
			printf("\nsocket descriptor inserted into job buffer: %d\n", jobBuff[jobRear]); // FOR TESTING
			#endif
			jobRear = (jobRear + 1) % JOB_BUF_LEN; // reset jobRear when it reaches the 100th index (this makes it circular)
		} 

		pthread_mutex_unlock(&job_mutex); // unlock job mutex
		pthread_cond_signal(&job_cv_cs); // signal job buffer NOT EMPTY
		printf("Connection success!\n"); // print connection success to server
    // send message to client prompting them to enter a word to check
		send(clientSocket, clientMessage, strlen(clientMessage), 0); 
	}
	return 0;
}

//Function for worker Thread
void* workerThreadFunc(void* arg) {
	while (1) {
		// take socket descriptor out of job buffer to use
		pthread_mutex_lock(&job_mutex); // lock job mutex for job buffer	
    	// while loop to check if size of job buffer is empty
		while(jobCount == 0) {
      		// have the consumer (the worker thread) wait until the job buffer is signaled NOT empty
			pthread_cond_wait(&job_cv_cs, &job_mutex); 
		}
		#ifdef TESTING
		printf("job count of job buffer BEFORE removing: %d\n", jobCount); // FOR TESTING
		#endif

    	// socket descriptor to be removed from job buffer
		int socketDesc; 
		// Remove socket descriptor from job buffer to use
		if (jobCount == 0) { // if the job buffer is empty
			printf("Job buffer is empty! Can't remove anything!\n"); // print a message
		} else { // otherwise remove a socketDesc from the job buffer
			socketDesc = jobBuff[jobFront]; // store socket descriptor from job buffer into socketDesc to process
			jobFront = (jobFront + 1) % JOB_BUF_LEN; // reset jobFront when it reaches 100th index (this makes it circular)
			jobCount--; // decrement jobCount since a socket descriptor has been removed from job buffer at this point
		}

		#ifdef TESTING
		printf("job count of job buffer AFTER removing: %d\n", jobCount); // FOR TESTING
		#endif
		pthread_mutex_unlock(&job_mutex); // unlock job_mutex
		pthread_cond_signal(&job_cv_pd); // signal job buffer NOT full because we just took something out


		/**** RECEIVE A WORD TO USE FOR SPELL-CHECKING ****/
		// allocate memory for word you're going to receive from client with calloc(max size you'll accept, 1);
		char* word = calloc(MAX_WORD_SIZE, 1); 
		// using recv() with socketDesc we took out of job buffer, can then assume word from client has been received and stored in our allocated word var
		while(recv(socketDesc, word, MAX_WORD_SIZE, 0)) { 
			if (strlen(word) <= 1) { // if nothing was entered, continue
				continue;
			}

			#ifdef TESTING
			printf("\nRECEIVED WORD FROM CLIENT WITH SOCKET DESC: %d! Word received: %s", socketDesc, word); // FOR TESTING
			#endif

			// Exit client if escape entered
			if (word[0] == 27) { // if escape entered, exit this thread
				printf("Escape was entered! Exiting client with socket descriptor: %d..\n", socketDesc);  // print message
				write(socketDesc, msgClose, strlen(msgClose)); // write closing message to client
				close(socketDesc); // close client socket
				break;
			}

			// Search for word in dictionary
			if (searchForWordInDict(dictionary, word)) { // if word was found in dictionary
				strtok(word, "\n"); // take newline out of word for readability
				word = realloc(word, sizeof(char*)*PHRASE_SIZE); // realloc space for word to fit in 'correctness' concatenated on end, realloc takes care of freeing old memory for word
				strcat(word, " OK\n"); // concatenate OK onto end of the word
			} else { // word was not found :(
				strtok(word, "\n"); // take newline out of word for readability
				word = realloc(word, sizeof(char*)*PHRASE_SIZE); // realloc space for word to fit in 'correctness' concatenated on end, realloc takes care of freeing old memory for word
				strcat(word, " WRONG\n"); // concatenate WRONG onto end of the word
			}

			write(socketDesc, word, strlen(word)); // write a message with write() or send() to client with word plus correctness
			write(socketDesc, msgRequest, strlen(msgRequest)); // wr
		
			// Write phrase to log buffer using mutual exclusion for log buffer
			pthread_mutex_lock(&log_mutex); // lock log mutex
			while(logCount == LOG_BUF_LEN) {// while log buffer is full
				pthread_cond_wait(&log_cv_pd, &log_mutex); // wait for log buffer to NOT be full
			}

			// Add word (which is now the phrase 'word + correctness') to log buffer
			if (logCount == LOG_BUF_LEN) { // if the log buffer is full
				printf("Log buffer is FULL!\n"); // print that it's full
			} else {
        // if the log buffer is empty, make the item the first in the queue, update front and rear
				if (logCount == 0) {
					logFront = 0;
					logRear = 0;
				}
				strcpy(logBuff[logRear], word);// add the phrase to the log buffer in the correct 
				logCount++; // increment count of phrases in the log buffer
				#ifdef TESTING
				printf("phrase inserted into log buffer: %s", logBuff[logRear]); // FOR TESTING
				#endif
				 // reset logRear when it reaches the 100th index (this makes it circular)
				logRear = (logRear + 1) % LOG_BUF_LEN;
			}

			pthread_mutex_unlock(&log_mutex); // unlock log mutex
			pthread_cond_signal(&log_cv_cs); // signal log buffer NOT empty
			free(word); // free old word/phrase used to add to log buffer
			word = calloc(MAX_WORD_SIZE, 1); // calloc memory for new word to use
		}
		close(socketDesc); // close socketDesc after use for that client
	}
}

void* logThreadFunc(void* arg) {
	while(1) {
		/* take phrase out of log buffer with mutual exclusion
      by lock log mutex. Then while log buffer is empty,
      wait to consume from log buffer */
		pthread_mutex_lock(&log_mutex); 
		while(logCount == 0) { 
			pthread_cond_wait(&log_cv_cs, &log_mutex); 
		}

		/* Remove phrase from log buffer to be written to log file
      if the log buffer is empty, we print a message */
		char* phraseToAppend;
		if (logCount == 0) {
			printf("log buffer is empty! Can't remove anything!\n"); 
		} else { 
      // otherwise remove a phrase from the log buffer
      // store phrase to remove from log buffer in phrase and then reset logFront when it reaches 100th index
			phraseToAppend = logBuff[logFront]; 
			logFront = (logFront + 1) % LOG_BUF_LEN; 
			logCount--; // decrement count of phrases in log buffer
		}
		#ifdef TESTING
		printf("Phrase taken out of log buffer to append to log file: %s", phraseToAppend); // FOR TESTING
		#endif
    // open the log file for appending
		logFile_ptr = fopen("logFile.txt", "a"); 
		if (logFile_ptr == NULL) { // if the log file was not opened successfully
			printf("Error opening log file for appending!\n"); // print error message
			exit(1);
		}
    // append phrase taken out of log buffer to log file, fputs returns nonnegative integer (>= 0) on success and EOF on error
		if (fputs(phraseToAppend, logFile_ptr) >= 0) { 
			#ifdef TESTING
			printf("Successfully appended to log file!\n"); // FOR TESTING
			#endif
		} else { 
      // otherwise something went wrong appending phrase to log file
			printf("Something went wrong appending phrase from log buffer to log file..\n"); // print error message
		}
		fclose(logFile_ptr); // close file when done appending
		pthread_mutex_unlock(&log_mutex); // unlock log mutex
		pthread_cond_signal(&log_cv_pd); // signal log buffer not full since we took 
	}
}

//Function to search words from Dictionary.txt
int searchForWordInDict(char dictionary[][MAX_WORD_SIZE], char* wordToFind) {
	int i = 0; // index to keep track of words in dictionary as we iterate through it
  // while i is less than wordsInDictionary - 1 (since we're starting i at 0)
  // attempt to find wordToFind in dictionary, if it was found
	while(i < wordsInDictionary - 1) { 
		if (strcmp(dictionary[i], wordToFind) == 0) { 
			#ifdef TESTING
			printf("WORD FOUND!\n");
			#endif
			return 1; // return 1 that word was found
		}
		i++; // increment i to check next word in dictionary
	} 
	// Otherwise, word was not found
	#ifdef TESTING
	printf("Word NOT found!\n");
	#endif
	return 0; // return 0 to indicate word was not found in dictionary
}
