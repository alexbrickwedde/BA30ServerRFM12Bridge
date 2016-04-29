// 2012-07-01 <mveerman@no_spam_please_it-innovations.com> http://opensource.org/licenses/mit-license.php
//
// This program is a test / proof of concept for interfacing the Raspberry Pi with the Hope RF RFM12B
// transceiver unit.
//
// It is still a work in progress, use at your own risk!
//
// For the latest version of this source code, and for instructions on how to hook up the RFM12B to the
// GPIO pins of the Raspberry Pi, go to:
//
//     http://forum.jeelabs.net/node/1229
//
//
// This (messy :-)) code was written by Michel Veerman.
//
// Though still a work in progress, a thank you is in order for a few people:
//   - Jean-Claude Whippler (author of the JeeLib library, www.jeelabs.org)
//   - Chris Boot (who made the custom kernel with SPI support, www.bootc.net)
//   - Gordon Henderson (author of wiringPi, projects.drogon.net)
//   - The Raspberry Pi Foundation (great piece of hardware, www.raspberrypi.org)
//   - The nice folks on forum.jeelabs.net, hope you enjoy it!
//
// About the program:
//
// Well, it's a test / proof of concept, so don't expect a polished library / clean code yet ;-)
//
// What it does:
//  - initializes the GPIO ports, and sets up the IRQ pin
//  - initializes the SPI layer
//  - initializes the RFM12B (868 MHz), basically the same way a JeeNode does
//  - sends a JeeLib compatible package, with a payload of 2 bytes
//  - enters an endless loop, and dumps received packages on the screen
//    (please note that the leading 0xAA 0xAA 0xAA 0x2D 0xD4 are eaten by the
//     RFM12B, and hench are not printed on screen)
//
// Receiving seems to work reliably now (although it's using 100% CPU, because it's not
// interrupt driven yet). But I haven't checked the CRC bytes yet.
// 
// Sending seems to go right 60% of the time. Not sure why yet, so that's a work in progress...
// Could be that it's suffering from interference, so if your mileage is better, let me know!
// 
// Note:
//
// I spoke to Chris Boot, and it seems a new kernel with interrupt support for the
// GPIO pins is coming soon! That's excellent news of course, since I can rewrite the
// code, so that it won't be using 100% CPU in the receive loop anymore. Might also
// improve the sending of package if we're lucky...
//
//
// Keep an eye on the forum link at the top of the source for updates!
// 

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


  // setup RFM69 and optional reset
  RFM69 rfm69(false); // false = RFM69W, true = RFM69HW

  printf("--------------------------------------------------------------------------------\n");
  printf("Setting up to receive data\n");
  printf("--------------------------------------------------------------------------------\n");

  // init RF module and put it to sleep
  rfm69.init();
  rfm69.sleep();
  rfm69.dumpRegisters();

  // set output power
  rfm69.setPowerDBm(13); // +13 dBm

  printf("--------------------------------------------------------------------------------\n");
  printf("Start receiving data\n");
  printf("--------------------------------------------------------------------------------\n");

  unsigned char rx[64];
  while (1)
  {
    usleep(1500);

    // check if a packet has been received
    int bytesReceived = rfm69.receive(rx, sizeof(rx));
    if (bytesReceived > 0)
    {
      printf("%d bytes received.", bytesReceived);

      sendudp(rx, bytesReceived);
    }
  }
  return 0;
}

