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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;

#define PROXYPORT 2101

static void usage(char *name)
{
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s serialportdevice answerbytes [hex [hex...]]\n", name);
  fprintf(stderr, "  - sends specified hex bytes and then waits for specified number of answer bytes (-1=forever, use Ctrl-C to abort)\n");
  fprintf(stderr, "  %s serialportdevice\n", name);
  fprintf(stderr, "  - proxy mode: accepts connection on TCP port %d and forwards to/from serial\n", PROXYPORT);
}



int main(int argc, char **argv)
{
  if (argc<2) {
    // show usage
    usage(argv[0]);
    exit(1);
  }
  int daemonMode = FALSE;

  if (argc==2) {
    // daemon mode
    daemonMode = TRUE;
  }


  // open the serial port
	int serialfd, res;
	struct termios oldtio,newtio;

	char *serialportdevice = argv[1];

	int argIdx;
	int data;
	unsigned char byte;

	serialfd = open(serialportdevice, O_RDWR | O_NOCTTY);
	if (serialfd <0) {
	  perror(serialportdevice); exit(-1);
	}
	tcgetattr(serialfd,&oldtio); // save current port settings


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
	tcflush(serialfd, TCIFLUSH);
	tcsetattr(serialfd,TCSANOW,&newtio);


	if (daemonMode) {

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
      connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
      // wait for getting data from either side now
      while ((n = read(connfd, &byte, 1)) > 0) {
        // got a byte, send it
        printf("read %d, Transmitting byte : 0x%02X\n",n, byte);
        // send
        res = write(serialfd,&byte,1);
      }
      printf("Connection closed, waiting for new connection\n");
      close(connfd);
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
      res = write(serialfd,&byte,1);
    }

    while (numRespBytes<0 || numRespBytes>0) {       /* loop for input */
      res = read(serialfd,&byte,1);   /* returns after 1 chars have been input */
      printf("Received     byte : 0x%02X\n",byte);
      numRespBytes--;
    }
	}

	// done
	tcsetattr(serialfd,TCSANOW,&oldtio);
	// close
	close(serialfd);

	// return
	return 0;
}
