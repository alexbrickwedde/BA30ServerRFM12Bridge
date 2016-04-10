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
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <wiringPi.h>

// RF12 command codes
#define RF_RECEIVER_ON  0x82DD
#define RF_XMITTER_ON   0x823D
#define RF_IDLE_MODE    0x820D
#define RF_TXREG_WRITE  0xB800

// Device settings
static const char *device = "/dev/spidev0.0";
static uint8_t spi_mode = 0;
static uint8_t spi_bits = 8; // Must be 8-bit, as that's the only mode the SPI driver support
static uint32_t spi_speed = 2500000; // 2.5 MHz is the maximum rate the RF12 support (??)
// Going above this value gives wrong readings, and errors
// ...^^^ is this really true? it probably won't even reaching
//    the speed of 2.5 MHz...
static uint16_t spi_delay = 0;		// Must be 0, we don't want a delay

//
// Helper function for fatal errors
//
static void
pabort(const char *s)
{
  perror(s);
  abort();
}

//
// Helper function for dumping the status byte
//
void
dumpStatusByte(uint16_t result)
{
#ifdef DEBUG_STATUS

  printf("Cmd[$0000]: FFIT POR  FFOV WKUP EXT  LBD  FFEM RSSI DQD  CRL  ATGL SIGN OFFS\n");
  printf("             %01d    %01d    %01d    %01d    %01d    %01d    %01d    %01d    %01d    %01d    %01d    %01d   %03d\n"
      , (result >> 15) & 0x01
      , (result >> 14) & 0x01
      , (result >> 13) & 0x01
      , (result >> 12) & 0x01
      , (result >> 11) & 0x01
      , (result >> 10) & 0x01
      , (result >> 9) & 0x01
      , (result >> 8) & 0x01
      , (result >> 7) & 0x01
      , (result >> 6) & 0x01
      , (uint8_t) (result & 0x0F)
  );

#endif
}

//
// rf12_xferSend
//
void
rf12_xferSend(int fd, unsigned char *ptx_buf, unsigned char *prx_buf, int len)
{
  // At least one of the buffers should be assigned
  if ((ptx_buf == NULL) && (prx_buf == NULL))
    pabort("rf12_xfer: Both transmit and receive buffers are NULL!\n");

  //
  struct spi_ioc_transfer xfer[1];
  int status;

  // Clear spi_ioc_transfer structure
  memset(xfer, 0, sizeof(xfer));

  // Set up spi_ioc_transfer structure
  // ...rf12 uses full duplex, so we receive bits while we're sending...
  int count;
  for (count = 0; count < len / 2; count++)
  {
    // Wait for interrupt
    while (digitalRead(7) != 0)
      ;

    xfer[0].tx_buf = (unsigned long) (ptx_buf + (count * 2));
    xfer[0].rx_buf = (unsigned long) (prx_buf + (count * 2));
    xfer[0].len = 2;
    xfer[0].delay_usecs = spi_delay;
    xfer[0].speed_hz = spi_speed;
    xfer[0].bits_per_word = spi_bits;

    status = ioctl(fd, SPI_IOC_MESSAGE(1), xfer);
    if (status < 0)
    {
      pabort("SP_IOC_MESSAGE");
    }

    // Strangely enough this seems to help for sending, don't know why
    // Regardless, only about 40% of the messages really get out...
    delayMicroseconds(20);
  }

  return;
}

//
// rf12_xferCmd
//
// Full duplex, always sends and receives 2 bytes at the same time.
//
uint16_t rf12_xferCmd(int fd, uint16_t cmd)
{
  struct spi_ioc_transfer xfer[1];
  unsigned char tx_buf[2];
  unsigned char rx_buf[2];
  int status;

  // Clear spi_ioc_transfer structure
  memset(xfer, 0, sizeof(xfer));

  // Store command in buffer
  tx_buf[0] = (cmd >> 8) & 0xff;
  tx_buf[1] = (cmd) & 0xff;

  // Set up spi_ioc_transfer structure
  // ...rf12 uses full duplex, so we receive bits the
  //    while we're sending
  xfer[0].tx_buf = (unsigned long) tx_buf;
  xfer[0].rx_buf = (unsigned long) rx_buf;
  xfer[0].len = 2;
  xfer[0].delay_usecs = spi_delay;
  xfer[0].speed_hz = spi_speed;
  xfer[0].bits_per_word = spi_bits;

  status = ioctl(fd, SPI_IOC_MESSAGE(1), xfer);
  if (status < 0)
  {
    pabort("SPI_IOC_MESSAGE");
  }

  return (rx_buf[0] << 8) | rx_buf[1];

}

//
// rf12_initialize
//
void
rf12_initialize(int fd)
{

  printf("--------------------------------------------------------------------------------\n");
  printf("rf12_initialize()\n");
  printf("--------------------------------------------------------------------------------\n");

  // Read status byte
  rf12_xferCmd(fd, 0x0000);	// intitial SPI transfer added to avoid power-up problem
  rf12_xferCmd(fd, RF_TXREG_WRITE);	// in case we're still in OOK mode

  // Wait until RFM12B is out of power-up reset
  while (digitalRead(7) == 0)
  {
    rf12_xferCmd(fd, 0x0000);
    break;
  }

  rf12_xferCmd(fd, 0x80E7); //| (band << 4));// EL (ena TX), EF (ena RX FIFO), 12.0pF
  rf12_xferCmd(fd, 0xA67C);		// 868MHz
  rf12_xferCmd(fd, 0xC6BF);
  rf12_xferCmd(fd, 0x948C);
  rf12_xferCmd(fd, 0xC2AB);
  rf12_xferCmd(fd, 0xCA81);
  rf12_xferCmd(fd, 0xC4F7);
  rf12_xferCmd(fd, 0x9850);
  rf12_xferCmd(fd, 0xE000);
  rf12_xferCmd(fd, 0xC800);
  rf12_xferCmd(fd, 0xC0E0);
}

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

  int ret = 0;
  int fd;

  fd = open(device, O_RDWR);
  if (fd < 0)
    pabort("Can't open device");

  ret = ioctl(fd, SPI_IOC_WR_MODE, &spi_mode);
  if (ret == -1)
    pabort("Can't set SPI mode");

  ret = ioctl(fd, SPI_IOC_RD_MODE, &spi_mode);
  if (ret == -1)
    pabort("Can't set SPI mode");

  // Bits per word
  ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &spi_bits);
  if (ret == -1)
    pabort("Can't set bits per word");

  ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &spi_bits);
  if (ret == -1)
    pabort("Can't set bits per word");

  // Max speed hz
  ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
  if (ret == -1)
    pabort("Can't set max speed hz");

  ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
  if (ret == -1)
    pabort("Can't set max speed hz");

  printf("spi mode: %d\n", spi_mode);
  printf("bits per word: %d\n", spi_bits);
  printf("max speed: %d Hz (%d KHz)\n", spi_speed, spi_speed / 1000);

  rf12_initialize(fd);

  printf("--------------------------------------------------------------------------------\n");
  printf("Setting up to receive data\n");
  printf("--------------------------------------------------------------------------------\n");

  rf12_xferCmd(fd, 0xCA83);
  rf12_xferCmd(fd, RF_RECEIVER_ON);

  printf("--------------------------------------------------------------------------------\n");
  printf("Start receiving data\n");
  printf("--------------------------------------------------------------------------------\n");

  unsigned char buf[1024], *bp;
  int len = 0;
  unsigned int size = 5;
  while (1)
  {
    usleep(1500);
//		if (digitalRead(7) == 0)
//		{
    uint16_t status = rf12_xferCmd(fd, 0x0000);

    // Test if buffer is not empty (IRQ can occur for multiple reasons)
    while ((status & 0x0200) != 0x0200)
    {
      usleep(750);

      // Test for buffer overrun, and warn us about it
      if ((status & 0x2000) == 0x2000)
        printf("Buffer overrun (but most of the time, nothing is missing... weird...)!\n");

      // Check if at least 8 bits are in (FFIT flag)
      if ((status & 0x8000) == 0x8000)
      {
        dumpStatusByte(status);

        uint16_t byteRead = rf12_xferCmd(fd, 0xB000);

        // We stop storing bytes if a message is complete, or if the
        // maximum buffer size is reached
        if ((len <= 1023)) // && ( (len < 2) || ( (len >= 2) && (buf[1]+4 > len) ) ) )
        {
          buf[len] = byteRead & 0xff;
          len++;
        }
      }

      status = rf12_xferCmd(fd, 0x0000);

      if ((len == 5) && (size == 5))
      {
        switch (buf[4])
        {
        case 'M':
          size = 11;
          break;
        case 'e':
          size = 9;
          break;
        case 'f':
          size = 19;
          break;
        case 'L':
          size = 29;
          break;
        case 'T':
          size = 25;
          break;
        case 'g':
          size = 19;
          break;
        default:
          size = 6;
          break;
        }
        printf("size = (%02x): ", size);
      }
      if (len >= size)
      {
        break;
      }

    }
//		}

    if ((len >= 1024) || (len >= size))
    {
      rf12_xferCmd(fd, 0x8208);

      printf("read data(%02x): ", len);
      for (bp = buf; len; len--)
        printf(" %02x", *bp++);
      printf("\n");

      sendudp(buf, size);

      len = 0;
      size = 5;

      rf12_xferCmd(fd, 0x82C8);
      rf12_xferCmd(fd, 0xCA81);
      rf12_xferCmd(fd, 0xCA83);
    }
  }

  close(fd);
  return ret;
}

