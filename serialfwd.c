/*
 * serialforwarder.c
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
#include <errno.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP=FALSE;

#define DEFAULT_PROXYPORT 2101
#define DEFAULT_CONNECTIONPORT 2101
#define DEFAULT_BAUDRATE 9600


static void usage(char *name)
{
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s [-P port] [-b baudrate] (serialportdevice|ipaddr) answerbytes [hex [hex...]]\n", name);
  fprintf(stderr, "    sends specified hex bytes and then waits for specified number of answer bytes\n");
  fprintf(stderr, "    (answerbytes == INF: wait forever, use Ctrl-C to abort)\n");
  fprintf(stderr, "    -P port : port to connect to (default: %d)\n", DEFAULT_CONNECTIONPORT);
  fprintf(stderr, "  %s [-d] [-p servingport] [-P port] [-b baudrate] (serialportdevice|ipaddr)\n", name);
  fprintf(stderr, "    proxy mode: accepts TCP connection and forwards to/from serial/ipaddr\n");
  fprintf(stderr, "    -d : fully daemonize and suppress showing byte transfer messages on stdout\n");
  fprintf(stderr, "    -p servingport : port to accept connections from (default: %d)\n", DEFAULT_PROXYPORT);
  fprintf(stderr, "    -P port : port to connect to (default: %d)\n", DEFAULT_CONNECTIONPORT);
  fprintf(stderr, "    -b baudrate : baudrate when connecting to serial port (default: %d)\n", DEFAULT_BAUDRATE);
  fprintf(stderr, "    -w seconds : number of seconds to wait before (re)opening connections (default: 0)\n");
  fprintf(stderr, "    -D : activate DTR when connection opens, deactivate before closing\n");
}


static void daemonize(void)
{
  pid_t pid, sid;

  /* already a daemon */
  if ( getppid() == 1 ) return;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* At this point we are executing as the child process */

  /* Change the file mode mask */
  umask(0);

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  /* Change the current working directory.  This prevents the current
     directory from being locked; hence not being able to remove it. */
  if ((chdir("/")) < 0) {
    exit(EXIT_FAILURE);
  }

  /* Redirect standard files to /dev/null */
  freopen( "/dev/null", "r", stdin);
  freopen( "/dev/null", "w", stdout);
  freopen( "/dev/null", "w", stderr);
}


// globals
// - options and params from cmdline
char *outputname = NULL;
int proxyMode = FALSE;
int daemonMode = FALSE;
int serialMode = FALSE;
int controlDTR = FALSE;
int verbose = TRUE;
int proxyPort = DEFAULT_PROXYPORT;
int connPort = DEFAULT_CONNECTIONPORT;
int baudRate = DEFAULT_BAUDRATE;
int startupDelay = 0;

// outgoing connection
int outputfd = -1;
int baudRateCode = B0;
struct termios oldtio; // saved termios to restore at exit


void openOutgoing()
{
  if (outputfd<0) {
    // Open serial or TCP outgoing connection
    int res;
    struct termios newtio;

    if (serialMode) {
      if (verbose) printf("Opening outgoing serial connection to %s\n",outputname);

      outputfd = open(outputname, O_RDWR | O_NOCTTY);
      if (outputfd <0) {
        perror(outputname); exit(-1);
      }
      tcgetattr(outputfd,&oldtio); // save current port settings
      // see "man termios" for details
      memset(&newtio, 0, sizeof(newtio));
      // - 8-N-1, no modem control lines (local), reading enabled
      newtio.c_cflag = CS8 | CLOCAL | CREAD;
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
      // - set speed (as this ors into c_cflag, this must be after setting c_cflag initial value)
      cfsetspeed(&newtio, baudRateCode);
      // - set new params
      tcflush(outputfd, TCIFLUSH);
      tcsetattr(outputfd,TCSANOW,&newtio);
      // - set DTR if requested
      if (controlDTR) {
        int controlbits = TIOCM_DTR;
        ioctl(outputfd, (TIOCMBIS), &controlbits);
      }
    }
    else {
      if (verbose) printf("Opening outgoing TCP connection to %s\n",outputname);
      // assume it's an IP address or hostname
      struct sockaddr_in conn_addr;
      if ((outputfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error: Could not create socket\n");
        exit(1);
      }
      // prepare IP address
      memset(&conn_addr, '0', sizeof(conn_addr));
      conn_addr.sin_family = AF_INET;
      conn_addr.sin_port = htons(connPort);

      struct hostent *server;
      server = gethostbyname(outputname);
      if (server == NULL) {
        printf("Error: no such host");
        exit(1);
      }
      memcpy((char *)&conn_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);

      if ((res = connect(outputfd, (struct sockaddr *)&conn_addr, sizeof(conn_addr))) < 0) {
        printf("Error: %s\n", strerror(errno));
        exit(1);
      }
    }
  }
}


void closeOutgoing()
{
  if (outputfd>=0) {
    if (serialMode) {
      if (verbose) printf("Closing outgoing serial connection to %s\n",outputname);
      // - clear DTR if requested
      if (controlDTR) {
        int controlbits = TIOCM_DTR;
        ioctl(outputfd, (TIOCMBIC), &controlbits);
      }
      // restore settings
      tcsetattr(outputfd,TCSANOW,&oldtio);
    }
    else {
      if (verbose) printf("Closing outgoing TCP connection to %s\n",outputname);
    }
    // close
    close(outputfd);
    outputfd = -1;
  }
}



int main(int argc, char **argv)
{
  if (argc<2) {
    // show usage
    usage(argv[0]);
    exit(1);
  }

  int c;
  while ((c = getopt(argc, argv, "hdDp:P:b:w:")) != -1)
  {
    switch (c) {
      case 'h':
        usage(argv[0]);
        exit(0);
      case 'd':
        daemonMode = TRUE;
        verbose = FALSE;
        break;
      case 'D':
        controlDTR = TRUE;
        break;
      case 'p':
        proxyPort = atoi(optarg);
        break;
      case 'P':
        connPort = atoi(optarg);
        break;
      case 'b':
        baudRate = atoi(optarg);
        break;
      case 'w':
        startupDelay = atoi(optarg);
        break;
      default:
        exit(-1);
    }
  }
  // proxymode is when only one arg is here
  if (argc-optind == 1 || daemonMode) {
    proxyMode = TRUE;
  }

  int argIdx;
  outputname = argv[optind++];
  serialMode = *outputname=='/';

  // check type of input
  if (serialMode) {
    // assume it's a serial port
    switch (baudRate) {
      case 50 : baudRateCode = B50; break;
      case 75 : baudRateCode = B75; break;
      case 134 : baudRateCode = B134; break;
      case 150 : baudRateCode = B150; break;
      case 200 : baudRateCode = B200; break;
      case 300 : baudRateCode = B300; break;
      case 600 : baudRateCode = B600; break;
      case 1200 : baudRateCode = B1200; break;
      case 1800 : baudRateCode = B1800; break;
      case 2400 : baudRateCode = B2400; break;
      case 4800 : baudRateCode = B4800; break;
      case 9600 : baudRateCode = B9600; break;
      case 19200 : baudRateCode = B19200; break;
      case 38400 : baudRateCode = B38400; break;
      case 57600 : baudRateCode = B57600; break;
      case 115200 : baudRateCode = B115200; break;
      case 230400 : baudRateCode = B230400; break;
      #ifndef __APPLE__
      case 460800 : baudRateCode = B460800; break;
      #endif
      default:
        fprintf(stderr, "invalid baudrate %d (standard baudrates 50..460800 are supported)\n", baudRate);
        exit(1);
    }
  }

  // daemonize now if requested and in proxy mode
  if (daemonMode && proxyMode) {
    printf("Starting background daemon listening on port %d for incoming connections\n",proxyPort);
    daemonize();
  }

  do {

    if (startupDelay>0) {
      if (verbose) printf("Waiting %d seconds before opening any connections\n", startupDelay);
      sleep(startupDelay);
    }

    int data;
    unsigned char byte;
    int res;

    if (proxyMode) {

      int listenfd = 0, servingfd = 0;
      struct sockaddr_in serv_addr;

      fd_set readfs;    /* file descriptor set */
      fd_set errorfs;    /* file descriptor set */
      int    maxrdfd;     /* maximum file descriptor used */

      const size_t bufsiz = 200;
      unsigned char buffer[bufsiz];
      int numBytes;
      int gotBytes;
      int i;

      int n;

      listenfd = socket(AF_INET, SOCK_STREAM, 0);
      memset(&serv_addr, '0', sizeof(serv_addr));

      serv_addr.sin_family = AF_INET;
      serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      serv_addr.sin_port = htons(proxyPort); // port

      bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

      listen(listenfd, 1); // max one connection for now

      if (verbose) printf("Proxy mode, listening on port %d for incoming connections\n",proxyPort);

      int terminated = FALSE;
      while (!terminated) {
        if (verbose) printf("Waiting for new incoming connection...\n");
        // accept the connection, open fd
        servingfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
        if (verbose) printf("Accepted connection\n");
        // open outgoing connection now
        openOutgoing();
        // wait for getting data from either side now
        while (TRUE) {
          // prepare fd observation using select()
          maxrdfd = MAX (outputfd, servingfd)+1;  /* maximum bit entry (fd) to test */
          FD_ZERO(&readfs);
          FD_ZERO(&errorfs);
          FD_SET(servingfd, &readfs);  /* set testing for serving connection */
          FD_SET(outputfd, &readfs);  /* set testing for serial/tcp client connection */
          FD_SET(servingfd, &errorfs);  /* set testing for serving connection */
          FD_SET(outputfd, &errorfs);  /* set testing for serial/tcp client connection */
          // block until input becomes available
          n = select(maxrdfd, &readfs, NULL, &errorfs, NULL);
          if (n<0) {
            if (verbose) printf("select error %s -> aborting\n", strerror(errno));
            terminated = TRUE;
            break;
          }
          if (FD_ISSET(servingfd,&errorfs)) {
            // error occurred on served connection
            if (verbose) printf("Error on incoming connection %s -> disconnect\n", strerror(errno));
            break;
          }
          if (FD_ISSET(outputfd,&errorfs)) {
            // error occurred on served connection
            if (verbose) printf("Error on outgoing connection %s -> aborting\n", strerror(errno));
            terminated = TRUE;
            break;
          }
          if (FD_ISSET(servingfd,&readfs)) {
            // input from served TCP connection available
            // - get number of bytes available
            n = ioctl(servingfd, FIONREAD, &numBytes);
            if (n<0 || numBytes<=0) break; // connection closed
            // limit to max buffer size
            if (numBytes>bufsiz)
              numBytes = bufsiz;
            // read
            gotBytes = 0;
            if (numBytes>0)
              gotBytes = read(servingfd,buffer,numBytes); // read available bytes
            if (gotBytes<1) break; // connection closed
            // got bytes, send them
            if (verbose) {
              printf("Transmitting : ");
              for (i=0; i<gotBytes; ++i) {
                printf("0x%02X ", buffer[i]);
              }
              printf("\n");
            }
            // send to serial client
            res = write(outputfd,buffer,gotBytes);
          }
          if (FD_ISSET(outputfd,&readfs)) {
            // input from serial/client connection available
            // - get number of bytes available
            n = ioctl(outputfd, FIONREAD, &numBytes);
            if (n<0) {
              if (verbose) printf("ioctl FIONREAD error on outgoing connection %s -> aborting\n", strerror(errno));
              terminated = TRUE;
              break;
            }
            if (numBytes<=0) {
              if (verbose) printf("ioctl FIONREAD indicates 0 bytes ready on outgoing connection -> aborting\n");
              terminated = TRUE;
              break;
            }
            // limit to max buffer size
            if (numBytes>bufsiz)
              numBytes = bufsiz;
            // read
            gotBytes = 0;
            if (numBytes>0)
              gotBytes = read(outputfd,buffer,numBytes); // read available bytes
            if (gotBytes<0) {
              if (verbose) printf("read error on outgoing connection %s -> aborting\n", strerror(errno));
              terminated = TRUE;
              break;
            }
            if (gotBytes<1) {
              if (verbose) printf("read  on outgoing connection returns 0 bytes -> keep waiting\n");
              continue;
            }
            // got bytes, send them
            if (verbose) {
              printf("Received     : ");
              for (i=0; i<gotBytes; ++i) {
                printf("0x%02X ", buffer[i]);
              }
              printf("\n");
            }
            // send to server
            res = write(servingfd,buffer,gotBytes);
          }
        }
        close(servingfd);
        closeOutgoing();
        if (verbose) printf("Incoming connection was closed, waiting for new connection\n");
      }
    }
    else {
      // command line direct mode
      openOutgoing();
      int numRespBytes = 0;
      if (strcmp(argv[optind],"INF")==0) {
        // wait indefinitely
        numRespBytes = -1;
      }
      else {
        // parse number of bytes expected
        sscanf(argv[optind],"%d",&numRespBytes);
      }
      optind++;
      // parse and send the input bytes
      for (argIdx=optind; argIdx<argc; argIdx++) {
        // parse as hex
        sscanf(argv[argIdx],"%x",&data);
        byte = data;
        // show
        if (verbose) printf("Transmitting byte : 0x%02X\n",data);
        // send
        res = write(outputfd,&byte,1);
      }

      while (numRespBytes<0 || numRespBytes>0) {       /* loop for input */
        res = read(outputfd,&byte,1);   /* returns after 1 chars have been input */
        if (res<0) {
          if (verbose) printf("read error %s -> aborting\n", strerror(errno));
          break;
        }
        else if (res==0) {
          if (verbose) printf("connection was closed, nothing to read any more -> aborting\n");
          break;
        }
        if (verbose) printf("Received     byte : 0x%02X\n",byte);
        numRespBytes--;
      }
      closeOutgoing();
    }
  } while (proxyMode);

  // return
  return 0;
}
