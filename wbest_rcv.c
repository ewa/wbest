// wbest_rcv.c: the receiver of WBest (V1.0) in Linux
//
// $Id: wbest_rcv.c,v 1.7 2006/07/28 19:49:59 lmz Exp $
//
// Author:  Mingzhe Li (lmz@cs.wpi.edu)
//          Computer Science Dept.
//          Worcester Polytechnic Institute (WPI)
//
// History
//   Version 1.0 - 2006-05-10
//
// Description:
//       wbest_rcv.c
//              Receiver of WBest, which is a bandwidth estimation tool
//              designed for wireless streaming application.
//              Wireless Bandwidth ESTimation (WBest)
//
// Compile and Link:
//       make
//
// License:
//   This software is released into the public domain.
//   You are free to use it in any way you like.
//   This software is provided "as is" with no expressed
//   or implied warranty.  I accept no liability for any
//   damage or loss of business that this software may cause.
////////////////////////////////////////////////////////////////////////////

#include "wbest.h"

// global variable
int tcpSocket, udpSocket;                  // TCP/UDP socket  
struct timeval arrival[MAX_PKT_NUM];       // arrrival time
int sendtime[MAX_PKT_NUM];                 // sending time
int seq[MAX_PKT_NUM];                      // sequence number
int psize[MAX_PKT_NUM];                    // packet size (bytes)
int disperse[MAX_PKT_NUM];                 // dispersion(usec)
double ce[MAX_PKT_NUM];                    // Effective capacity (Mbps)
double sr[MAX_PKT_NUM];                    // Sending rate (Mbps)
int ceflag[MAX_PKT_NUM];                   // valid packet pair
double allCE = 0., allAT = 0., allAB =0.;

// Function prototype
void TCPServer(int nPort);                             // setup the TCP server 
void UDPServer(int nPort);                             // the UDP server 
void UDPReceive (enum Options option, int i_PktNumb);  // Receive UDP packet (PP/PT) 
double ProcessPP (int i_PktNumb);                      // Process PP data
double ProcessPT (int i_PktNumb);                      // Process PT data
void InitStorage() ;                                   // initial array
void CleanUp(int arg);                                     // handle for ctrl_c, clean up
void sort_int (int arr[], int num_elems);              // Sort int array
void sort_double (double arr[], int num_elems);        // Sort double array

//////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
  int nRet = 0;
  int nPortUDP = 1234;
  int nPortTCP = 9878;
  int i_PktNumb; 
  struct Ctl_Pkt control_pkt;
  int c;

  //use getopt to parse the command line
  while ((c = getopt(argc, argv, "p:")) != EOF)
    {
      switch (c)
        {
        case 'p':
          printf("udp portnumber: %d\n", atoi(optarg));
          nPortUDP = atoi(optarg);
          break;

        case '?':
          printf("ERROR: illegal option %s\n", argv[optind-1]);
          printf("Usage:\n");
          printf("\t%s -p udp_portnumber\n", argv[0]);
          exit(1);
          break;

        default:
          printf("WARNING: no handler for option %c\n", c);
          printf("Usage:\n");
          printf("\t%s -p udp_portnumber\n", argv[0]);
          exit(1);
          break;
        }
    }
  

  signal(SIGINT, CleanUp);

  UDPServer(nPortUDP);
  TCPServer(nPortTCP);

  while(1) 
    {
      nRet = recv(tcpSocket, (char *) &control_pkt, sizeof(control_pkt), 0);
      if (nRet <= 0 ) // peer closed
	{
	  break;
	}
      
      // what does the client want to do? PP/PT?
      if (nRet!=sizeof(control_pkt) || (control_pkt.option != PacketPair && control_pkt.option!=PacketTrain)) 
	{
	  printf("Receive unknow message %d, with size %d\n", control_pkt.option, nRet);
	  exit(1);
	}

      i_PktNumb = control_pkt.value;
  
      switch (control_pkt.option) 
	{
	case PacketPair:
	  control_pkt.option = Ready;
	  if (send(tcpSocket, (char *) &control_pkt, sizeof(control_pkt), 0) != sizeof(control_pkt))
	    {
	      perror("Send TCP control packet error");
	      exit(1);
	    }
	  // receive PP
	  UDPReceive(PacketPair, i_PktNumb); 

	  control_pkt.option = PacketPair;
	  allCE = ProcessPP(i_PktNumb); 
	  control_pkt.value = (unsigned int) (allCE * 1000000);

	  if (send(tcpSocket, (char *) &control_pkt, sizeof(control_pkt), 0) != sizeof(control_pkt))
	    {
	      perror("Send TCP control packet error");
	      exit(1);
	    }
	  break;

	case PacketTrain:
	  control_pkt.option = Ready;
	  if (send(tcpSocket, (char *) &control_pkt, sizeof(control_pkt), 0) != sizeof(control_pkt))
	    {
	      perror("Send TCP control packet error");
	      exit(1);
	    }

	  // receive PT
	  UDPReceive(PacketTrain, i_PktNumb);  

	  control_pkt.option = PacketTrain;
	  allAB = ProcessPT(i_PktNumb);
      
	  control_pkt.value = (unsigned int)(allAB * 1000000);

	  if (send(tcpSocket, (char *) &control_pkt, sizeof(control_pkt), 0) != sizeof(control_pkt))
	    {
	      perror("Send TCP control packet error");
	      exit(1);
	    }
	  break;

	default:
	  break;
	}
    } //end while(1)

  CleanUp (0);
  return 0;
} // end main()

////////////////////////////////////////////////////////////////////////////////////
void TCPServer (int nPort)
{
  int listenSocket;
  struct sockaddr_in saTCPServer, saTCPClient;              // TCP address
  int nRet;                                                 // result
  int nLen;                                                 // length
  char szBuf[4096];                                         // client name

  listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listenSocket < 0)
    {
      perror("Failed to create listensocket create");
      exit(1);
    }

  saTCPServer.sin_family = AF_INET;
  saTCPServer.sin_addr.s_addr = INADDR_ANY; // Let WinSock assign address
  saTCPServer.sin_port = htons(nPort);   // Use port passed from user


  // bind the name to the socket
  nRet = bind(listenSocket, (struct sockaddr *) &saTCPServer, sizeof(struct sockaddr));
  if (nRet < 0)
    {
      perror ("Bind TCP server socket failed");
      close(listenSocket);
      exit(1);
    }
  
  if (listen(listenSocket, 5) < 0) {
    perror ("Listen Failed");
    exit(1);
  }

  // printing out where the server is waiting
  nRet = gethostname(szBuf, sizeof(szBuf));
  if (nRet < 0)
    {
      perror("Failed to get the server name");
      close(listenSocket);
      exit(1);
    }

  // Show the server name and port number
  printf("\nTCP Server named %s listening on port %d\n", szBuf, nPort);
  
  /* Set the size of the in-out parameter */
  nLen = sizeof(saTCPClient);

  /* Wait for a client to connect */
  tcpSocket = accept(listenSocket, (struct sockaddr *) &saTCPClient,  &nLen);
  if (tcpSocket < 0)
    {
      perror("Failed to sccept client connection");
      close(listenSocket);
      exit(1);
    }
  
  close (listenSocket);
  
} // end TCPServer()

/////////////////////////////////////////////////////////////////////////////
void UDPServer(int nPort)
{

  int nRet;
  struct sockaddr_in saUDPServer;
  char szBuf[4096];

  udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (udpSocket < 0)
    {
      perror("Failed to create UDP socket");
      exit(1);
    }

  // Fill in the address structure
  saUDPServer.sin_family = AF_INET;
  saUDPServer.sin_addr.s_addr = INADDR_ANY; // Let Socket assign address
  saUDPServer.sin_port = htons(nPort);      // Use port passed from user

  // bind the name to the socket
  nRet = bind(udpSocket, (struct sockaddr *) &saUDPServer, sizeof(struct sockaddr));
  if (nRet < 0)
    {
      perror ("Failed to bind UDP socket");
      close (udpSocket);
      exit(1);
    }


  // Show where the server is waiting
  nRet = gethostname(szBuf, sizeof(szBuf));
  if (nRet < 0)
    {
      perror ("Failed to get local host name");
      close (udpSocket);
      exit(1);
    }

  // Show the server name and port number
  printf("UDP Server named %s waiting on port %d\n",
	 szBuf, nPort);
} // end UDPServer()

/////////////////////////////////////////////////////////////////////////////
void UDPReceive (enum Options option, int i_PktNumb)
{
  int nLen, inum = 0, nSelect = 0;
  char szBuf[4096];
  int nRet, errgen=0;
  struct sockaddr_in saUDPClient;
  struct timeval timeout;           // Timeout for select
  fd_set rfds;

  nLen = sizeof(saUDPClient);

  // Initial storage, wait for data from the client
  InitStorage(); 

  // if packetpair, double the packet number.
  if (option == PacketPair) i_PktNumb = 2 * i_PktNumb;

  // Use select to control timeout for 300 ms
  FD_ZERO(&rfds);
  FD_SET(udpSocket, &rfds);

  while(inum < i_PktNumb){
    timeout.tv_sec = 0;             // reset the timout value
    timeout.tv_usec = 300000;       // to 300 ms

    memset(szBuf, 0, sizeof(szBuf));
    nSelect = select(udpSocket+1, &rfds, NULL, NULL, &timeout);
    if ( nSelect < 0) {
      perror("Failed in select function");
      break;
    } 
    else if (nSelect == 0) {
      printf("Receiving UDP packets timeout (300 ms).\n");
      break;
    }
    else {
      nRet = recvfrom(udpSocket,             // Bound socket
		      szBuf,                 // Receive buffer
		      sizeof(szBuf),         // Size of buffer in bytes
		      0,                     // Flags
		      (struct sockaddr *)&saUDPClient, // Buffer to receive client address 
		      &nLen);                // Length of client address buffer
      ////////////////////////
      //   if (errgen%6 == 0) nRet = -1;// Robust test for randomly packet lost at receiver.
      //   if (errgen > 15 && errgen < 25) nRet = -1;// Robust test for bursty lost at receiver.
           errgen++; // for test
      ////////////////////

      if (nRet > 0) {
	gettimeofday(&arrival[inum], NULL);
	seq[inum] = ((struct PP_Pkt *) &szBuf)->seq;
	sendtime[inum] = ((struct PP_Pkt *) &szBuf)->tstamp;
	psize[inum] = nRet;
	if (seq[inum] == i_PktNumb - 1) break;
	inum++;
      }
    }
  }
  return;

} // end UDPReceive()

/////////////////////////////////////////////////////////////////////////////
double ProcessPP (int i_PktNumb)
{
  // Show that we've received some data
  int processed = 0, count=0, i=0;
  double sum = 0., mean = 0., median=0.;

  for (i=0; i<i_PktNumb*2-1; i++) 
    {
      if (seq[i] == seq[i+1] && seq[i] >= 0 ) //packet pair not lost
	{
	  if ( processed > seq[i]) continue; // duplicated packet
	  while (processed < seq[i])        // we skip any pairs?
	    {
	      ceflag[processed] = 0;                                 // this pair is not valid
	      printf("[%2d]: Packet pair lost\n", processed);
	      processed ++;
	    }

    	  ceflag[processed] = 1;                                     // this pair is valid

	  disperse[count] = (arrival[i+1].tv_sec - arrival[i].tv_sec) * 1000000 +
	    (arrival[i+1].tv_usec - arrival[i].tv_usec);

	  ce[count] = (psize[i+1]*8.0/disperse[count]);              // compute effective capacity
	  sr[count] = psize[i+1]*8.0/(sendtime[i+1]-sendtime[i]);    // compute sending rate
	  
	  if (ce[count] > 0.) 
	    {
	      sum += ce[count];
	      printf("[%2d]: %d recv in %d usec - Ce: %7.2f Mbps, sendRate: %7.2f Mbps\n", 
		     seq[i+1], psize[i+1], disperse[count], ce[count], sr[count]);

	      count ++ ; // increase valid packet pair by 1
	    }

	  processed ++;  // Move to next pair

	  if (processed >= i_PktNumb) break;
	}
    }
  
  mean = sum/count;

  sort_double(ce, count);
  median = (ce[count/2]+ce[count/2+1])/2;

  printf("Summary of Ce test with %d valid tests out %d pairs:\n\tmedian: %f Mbps\n", 
	 count, i_PktNumb, median);
  if (median >= 0) 
    {
      return median; 
    }
  else
    {
      return 0.0;
    }
} // end ProcessPP()

/////////////////////////////////////////////////////////////////////////////
double ProcessPT (int i_PktNumb)
{
  int i=0, count=0, expected = 0, loss=0;
  double at[MAX_PKT_NUM], invalidrate, lossrate;
  int sum = 0;
  double medianAt=0., meanAt=0.;
  double medianAb=0., meanAb=0.;

  for (i=0; i<i_PktNumb; i++) 
    {
      if (seq[i] != expected) // Sequence number is not the one we expected (out of order=loss)
	{
	  printf ("[%2d](%2d-%2d): Dispersion invalid due to packet #%d lost (match 1)! \n", 
		  expected, expected, expected+1, expected); // the expected packet is lost
	  loss++;
	  if (seq[i] > expected) // Bursty loss
	    {
	      expected++;
	      while (seq[i] > expected && expected < i_PktNumb) 
		{
		  printf ("[%2d](%2d-%2d): Dispersion invalid due to packet #%d lost (match 2)! \n", 
			  expected, expected, expected+1, expected); // more losses after the first loss
		  loss++;
		  expected++;
		}
	    }
	}
      
      if (seq[i+1] == seq[i] + 1 && seq[i] == expected) // Good pkts, count the total good ones
	{
	  disperse[count] = (arrival[i+1].tv_sec - arrival[i].tv_sec) * 1000000 +
	    (arrival[i+1].tv_usec - arrival[i].tv_usec);

	  
	  at[count] = (psize[i+1]*8.0/disperse[count]);              // compute achievable throughput
	  sr[count] = psize[i+1]*8.0/(sendtime[i+1]-sendtime[i]);    // compute sending rate
          // dispersion rate should less than sending rate
	  if (at[count] > sr[count]) disperse[count] = psize[i+1]*8.0/sr[count];          

	  sum += disperse[count]; 

	  // Todo: maybe we need to filter out these sending error -- if (sr < ce) => discard?
	  
	  printf("[%2d](%2d-%2d): %d recv in %d usec - At: %7.2f Mbps, sendRate: %7.2f Mbps\n", 
		 expected, expected, expected+1, psize[i+1], disperse[count], at[count], sr[count]);
	  count++;
	}
      else // expected packet is good, however, the next one lost
	{
	  printf ("[%2d](%2d-%2d): Dispersion invalid due to packet #%d lost (next 1)! \n", 
		  expected, expected, expected+1, expected+1); // the next one packet is lost
	  if (expected == i_PktNumb -2) loss++; // Last packet in the train is lost... 
	}
      expected ++;
      if (expected >= i_PktNumb -1) break;
    }

  // in general, one packet loss = two dispersion loss
  // Todo: we can estimate the bursty loss be compare lossrate and invalidrate.
  //
  lossrate = (double)loss/(double)i_PktNumb;                            // loss rate of pkt
  invalidrate = (double)(i_PktNumb - 1 - count)/(double)(i_PktNumb-1);  // loss rate of dispersion

  printf("Summary of At test with %d valid tests out %d train (%d tests):\n",
         count, i_PktNumb, i_PktNumb - 1);
  printf("\tpacket loss: %d (%f%%) \n\tinvalid result: %d (%f%%)\n",
	 loss, lossrate * 100, i_PktNumb - 1 - count, invalidrate * 100);

  sort_int(disperse, count);
  
  meanAt = (double)psize[0] * 8.0 / ((double)sum / (double)count);
  medianAt = (double)psize[0] * 8.0 / (((double)disperse[count/2] + (double)disperse[count/2+1]) / 2.0);
  //printf("\tmean At: %f Mbps\n\tmedian At: %f Mbps\n", meanAt, medianAt);
  printf("\tmean At: %f Mbps\n", meanAt);

  // Equations... need to play around to compare the performance.
  // And to return At if the At is less than half Ce
  if (meanAt >= allCE ) 
    {
       meanAb = meanAt;
    }
  else
    {
      meanAb = allCE * (2.0 - allCE/meanAt);
    }

  
  if (medianAt >= allCE )
    {
       medianAb = medianAt;
    }
  else
    {
      medianAb = allCE * (2.0 - allCE/medianAt);
    }
  /*
  printf("\tmean Ab: %f Mbps\n\tmedian Ab: %f Mbps\n", meanAb, medianAb);
  printf("\tmean Ab with loss: %f Mbps\n\tmedian Ab with loss: %f Mbps\n", meanAb*(1.0-lossrate), medianAb*(1.0-lossrate));
  */

  printf("\tmean Ab: %f Mbps\n", meanAb);
  printf("\tmean Ab with loss: %f Mbps\n", meanAb*(1.0-lossrate));

  if (meanAb < 0) 
    {
      return 0; 
    }
  else 
    {
      return meanAb*(1.0-lossrate);
    }
      
} // end ProcessPT()

/////////////////////////////////////////////////////////////////////////////
void InitStorage()
{
  struct timeval notime;
  int i = 0;
  notime.tv_sec = 0;
  notime.tv_usec = 0;

  for (i=0; i<MAX_PKT_NUM; i++)
    {
      seq[i] = -1;
      sendtime[i] = -1;
      psize[i]= -1;
      arrival[i] = notime;
      disperse[i] = -1;
      ce[i] = -1;
      ceflag[i] = -1;
    }
} // end InitStorage()

/////////////////////////////////////////////////////////////////
void sort_double (double arr[], int num_elems)
{
  int i,j;
  double temp;

  for (i=1; i<num_elems; i++) {
    for (j=i-1; j>=0; j--)
      if (arr[j+1] < arr[j])
	{
	  temp = arr[j];
	  arr[j] = arr[j+1];
	  arr[j+1] = temp;
	}
      else break;
  }
} // end sort_double()

/////////////////////////////////////////////////////////////////
void sort_int (int arr[], int num_elems)
{
  int i,j;
  int temp;

  for (i=1; i<num_elems; i++) {
    for (j=i-1; j>=0; j--)
      if (arr[j+1] < arr[j])
	{
	  temp = arr[j];
	  arr[j] = arr[j+1];
	  arr[j+1] = temp;
	}
      else break;
  }
} // end sort_int()

/////////////////////////////////////////////////////////////////////////////
void CleanUp(int arg1) 
{
  close (tcpSocket);
  close (udpSocket);
  printf("WBest receiver is now off\n");
  exit(0);
} //end CleanUp()
