extern "C" {
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#include <wiringPi.h>
}

#include "rfm69.hxx"

extern void pabort(const char *s);

void
sendudp(unsigned char *buf, int size)
{
  int sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sd <= 0)
  {
    return;
  }

  int broadcastEnable = 1;
  int ret = setsockopt(sd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
  if (ret)
  {
    close(sd);
    return;
  }
  struct sockaddr_in broadcastAddr; // Make an endpoint
  memset(&broadcastAddr, 0, sizeof broadcastAddr);
  broadcastAddr.sin_family = AF_INET;
  inet_pton(AF_INET, "10.1.0.255", &broadcastAddr.sin_addr); // Set the broadcast IP address
  broadcastAddr.sin_port = htons(12345); // Set port 1900

  ret = sendto(sd, buf, size, 0, (struct sockaddr*) &broadcastAddr, sizeof broadcastAddr);
  if (ret < 0)
  {
    close(sd);
    return;
  }
  close(sd);
}

int
main(int argc, char *argv[])
{
  if (wiringPiSetup() == -1)
  {
    pabort("Failed to setup wiringPi");
  }

  pinMode(7, INPUT);
  pullUpDnControl(7, PUD_UP);


  RFM69 rfm69(false); // false = RFM69W, true = RFM69HW
  rfm69.init();
//  rfm69.dumpRegisters();
  rfm69.sleep();
  rfm69.setPowerDBm(13);

  unsigned char rx[64];
  while (1)
  {
    delay(10);

    int bytesReceived = rfm69.receive(rx, sizeof(rx));
    if (bytesReceived > 0)
    {
      printf("%d bytes received.\r\n", bytesReceived);
      sendudp(rx + 1, bytesReceived - 1);
    }

//    char testdata[] = {'0', '0', '0', '6', 'L', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd'};
//    int res = rfm69.send(testdata, sizeof(testdata));
  }
  return 0;
}
