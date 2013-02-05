/*
 * hello_world.c
 *
 * Description: Serial test
 *
 */


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/param.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;

#define PROXYPORT 2101

static void usage(char *name)
{
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s (serialportdevice|ipaddr) answerbytes [hex [hex...]]\n", name);
  fprintf(stderr, "  - sends specified hex bytes and then waits for specified number of answer bytes (-1=forever, use Ctrl-C to abort)\n");
  fprintf(stderr, "  %s (serialportdevice|ipaddr)\n", name);
  fprintf(stderr, "  - proxy mode: accepts connection on TCP port %d and forwards to/from serial/ipaddr\n", PROXYPORT);
}



int main(int argc, char **argv)
{
  if (argc<2) {
    // show usage
    usage(argv[0]);
    exit(1);
  }
  int proxyMode = FALSE;
  int serialMode = FALSE;

  if (argc==2) {
    // proxy mode
    proxyMode = TRUE;
  }

  int argIdx;
  int data;
  unsigned char byte;

  // Open input
  int inputfd, res;
  char *outputname = argv[1];
  struct termios oldtio,newtio;

  serialMode = *(argv[1])=='/';

  // check type of input
  if (serialMode) {
    // assume it's a serial port
    inputfd = open(outputname, O_RDWR | O_NOCTTY);
    if (inputfd <0) {
      perror(outputname); exit(-1);
    }
    tcgetattr(inputfd,&oldtio); // save current port settings

    // see "man termios" for details
    memset(&newtio, 0, sizeof(newtio));
    // - baudrate, 8-N-1, no modem control lines (local), reading enabled
    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
    // - ignore parity errors
    newtio.c_iflag = IGNPAR;
    // - no output control
    newtio.c_oflag = 0;
    // - no input control (non-canonical)
    newtio.c_lflag = 0;
    // - no inter-char time
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    // - receive every single char seperately
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 chars received */
    // - set new params
    tcflush(inputfd, TCIFLUSH);
    tcsetattr(inputfd,TCSANOW,&newtio);
  }
  else {
    // assume it's an IP address
    int sockfd = 0;
    struct hostent *server;
    struct sockaddr_in serv_addr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      printf("Error: Could not create socket\n");
      exit(1);
    }
    server = gethostbyname(outputname);
    if (server == NULL) {
      printf("Error: no such host");
      exit(1);
    }
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PROXYPORT);
    memcpy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);

//    if (inet_pton(AF_INET, outputname, &serv_addr.sin_addr)<=0) {
//      printf("\n inet_pton error occured\n");
//      exit(1);
//    }
    if (connect(inputfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
      printf("Error : Could not connect to %s\n", outputname);
      exit(1);
    }
  }

	if (proxyMode) {

    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;

    fd_set readfs;    /* file descriptor set */
    int    maxfd;     /* maximum file descriptor used */

    int n;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PROXYPORT); // port

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    listen(listenfd, 1); // max one connection for now

    printf("Proxy mode, listening on port %d for connections\n",PROXYPORT);

    while (TRUE) {
      // accept the connection, open fd
      connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
      // prepare fd observation using select()
      maxfd = MAX (inputfd, connfd)+1;  /* maximum bit entry (fd) to test */
      FD_ZERO(&readfs);
      // wait for getting data from either side now
      while (TRUE) {
        FD_SET(connfd, &readfs);  /* set testing for source 2 */
        FD_SET(inputfd, &readfs);  /* set testing for source 1 */
        // block until input becomes available
        select(maxfd, &readfs, NULL, NULL, NULL);
        if (FD_ISSET(connfd,&readfs)) {
          // input from TCP connection available
          n = read(connfd, &byte, 1);
          if (n<0) break; // connection closed
          // got a byte, send it
          printf("Transmitting byte : 0x%02X\n", byte);
          // send
          res = write(inputfd,&byte,1);
        }
        if (FD_ISSET(inputfd,&readfs)) {
          // input from serial available
          res = read(inputfd,&byte,1);   /* returns after 1 chars have been input */
          printf("Received     byte : 0x%02X\n", byte);
          res = write(connfd,&byte,1);
        }
      }
      close(connfd);
      printf("Connection closed, waiting for new connection\n");
    }
	}
	else {
    // command line direct mode
	  int numRespBytes = 0;
	  sscanf(argv[2],"%d",&numRespBytes);

	  // parse and send the input bytes
    for (argIdx=3; argIdx<argc; argIdx++) {
      // parse as hex
      sscanf(argv[argIdx],"%x",&data);
      byte = data;
      // show
      printf("Transmitting byte : 0x%02X\n",data);
      // send
      res = write(inputfd,&byte,1);
    }

    while (numRespBytes<0 || numRespBytes>0) {       /* loop for input */
      res = read(inputfd,&byte,1);   /* returns after 1 chars have been input */
      printf("Received     byte : 0x%02X\n",byte);
      numRespBytes--;
    }
	}

	// done
	if (serialMode) {
	  tcsetattr(inputfd,TCSANOW,&oldtio);
	}

	// close
	close(inputfd);

	// return
	return 0;
}
