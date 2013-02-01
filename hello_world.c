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

#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;


static void usage(char *name)
{
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s serialportdevice answerbytes [hex [hex...]]\n", name);
  fprintf(stderr, "  - sends specified hex bytes and then waits for specified number of answer bytes (-1=forever, use Ctrl-C to abort)\n");
}



int main(int argc, char **argv)
{
  if (argc<3) {
    // show usage
    usage(argv[0]);
    exit(1);
  }

  // open the serial port
	int fd, res, count;
	struct termios oldtio,newtio;

	char *serialportdevice = argv[1];
	int numRespBytes = 0;
	sscanf(argv[2],"%d",&numRespBytes);

	int argIdx;
	int data;
	unsigned char byte;

	fd = open(serialportdevice, O_RDWR | O_NOCTTY);
	if (fd <0) {
	  perror(serialportdevice); exit(-1);
	}
	tcgetattr(fd,&oldtio); // save current port settings


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
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd,TCSANOW,&newtio);

	// parse and send the input bytes
  for (argIdx=3; argIdx<argc; argIdx++) {
    // parse as hex
    sscanf(argv[argIdx],"%x",&data);
    byte = data;
    // show
    printf("Transmitting byte : 0x%02X\n",data);
    // send
    res = write(fd,&byte,1);
  }

	while (numRespBytes<0 || numRespBytes>0) {       /* loop for input */
	  res = read(fd,&byte,1);   /* returns after 1 chars have been input */
    printf("Received     byte : 0x%02X\n",byte);
    numRespBytes--;
	}

	// done
	tcsetattr(fd,TCSANOW,&oldtio);
	// close
	close(fd);

	// return
	return 0;
}
