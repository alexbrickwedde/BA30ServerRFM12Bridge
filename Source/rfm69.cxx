/**
 * @file rfm69.cpp
 *
 * @brief RFM69 and RFM69HW library for sending and receiving packets in connection with a STM32 controller.
 * @date January, February 2015
 * @author André Heßling
 *
 * This is a protocol agnostic driver library for handling HopeRF's RFM69 433/868/915 MHz RF modules.
 * Support is also available for the +20 dBm high power modules called RFM69HW/RFM69HCW.
 *
 * A CSMA/CA (carrier sense multiple access) algorithm can be enabled to avoid collisions.
 * If you want to enable CSMA, you should initialize the random number generator before.
 *
 * This library is written for the STM32 family of controllers, but can easily be ported to other devices.
 *
 * You have to provide your own functions for delay_ms() and mstimer_get().
 * Use the SysTick timer (for example) with a 1 ms resolution which is present on all ARM controllers.
 *
 * If you want to port this library to other devices, you have to provide an SPI instance
 * derived from the SPIBase class.
 */

/** @addtogroup RFM69
 * @{
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <wiringPi.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>

#include "rfm69.hxx"
#include "rfm69registers.h"

#define TIMEOUT_MODE_READY    100 ///< Maximum amount of time until mode switch [ms]
#define TIMEOUT_PACKET_SENT   100 ///< Maximum amount of time until packet must be sent [ms]
#define TIMEOUT_CSMA_READY    500 ///< Maximum CSMA wait time for channel free detection [ms]
#define CSMA_RSSI_THRESHOLD   -85 ///< If RSSI value is smaller than this, consider channel as free [dBm]

/** RFM69 base configuration after init().
 *
 * Change these to your needs or call setCustomConfig() after module init.
 */
static const uint8_t rfm69_base_config[][2] =
{
            {0x01, 0x04}, // RegOpMode: Standby Mode
            {0x02, 0x00}, // RegDataModul: Packet mode, FSK, no shaping
            {0x03, RF_BITRATEMSB_9600},
            {0x04, RF_BITRATELSB_9600},
            {0x05, RF_FDEVMSB_20000},
            {0x06, RF_FDEVLSB_20000},
            {0x07, 0xD9}, // RegFrfMsb: 868,3 MHz
            {0x08, 0x13}, // RegFrfMid
            {0x09, 0x33}, // RegFrfLsb
            {0x18, RF_LNA_GAINSELECT_AUTO},
            {0x19, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_20 | RF_RXBW_EXP_3}, // 20/2 -> 100khz
            {0x2C, 0x00}, // RegPreambleMsb: 3 bytes preamble
            {0x2D, 0x06}, // RegPreambleLsb
            {0x2E, RF_SYNC_ON | RF_SYNC_SIZE_4}, // RegSyncConfig: Enable sync word, 2 bytes sync word
            {0x2F, 0xDE}, // RegSyncValue1: 0xDEADBEEF
            {0x30, 0xAD}, // RegSyncValue2
            {0x31, 0xBE}, // RegSyncValue3
            {0x32, 0xEF}, // RegSyncValue4
            {0x37, 0xD0}, // RegPacketConfig1: Variable length, CRC on, whitening
            {0x38, 0x40}, // RegPayloadLength: 64 bytes max payload
            {0x3C, 0x8F}, // RegFifoThresh: TxStart on FifoNotEmpty, 15 bytes FifoLevel
            {0x58, 0x1B}, // RegTestLna: Normal sensitivity mode
            {0x6F, 0x30}, // RegTestDagc: Improved margin, use if AfcLowBetaOn=0 (default)

};

// Clock constants. DO NOT CHANGE THESE!
#define RFM69_XO               32000000    ///< Internal clock frequency [Hz]
#define RFM69_FSTEP            61


// Device settings
static const char *device = "/dev/spidev0.0";
static uint8_t spi_mode = 0;
static uint8_t spi_bits = 8; // Must be 8-bit, as that's the only mode the SPI driver support
static uint32_t spi_speed = 500000; // .5 MHz is the maximum rate the RF12 support (??)
// Going above this value gives wrong readings, and errors
// ...^^^ is this really true? it probably won't even reaching
//    the speed of 2.5 MHz...
static uint16_t spi_delay = 0;    // Must be 0, we don't want a delay

//
// Helper function for fatal errors
//
void pabort(const char *s)
{
  perror(s);
  abort();
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

uint16_t rf12_xferByte(int fd, uint8_t cmd)
{
  struct spi_ioc_transfer xfer[1];
  unsigned char tx_buf[1];
  unsigned char rx_buf[1];
  int status;

  // Clear spi_ioc_transfer structure
  memset(xfer, 0, sizeof(xfer));

  // Store command in buffer
  tx_buf[0] = cmd;

  xfer[0].tx_buf = (unsigned long) tx_buf;
  xfer[0].rx_buf = (unsigned long) rx_buf;
  xfer[0].len = 1;
  xfer[0].delay_usecs = spi_delay;
  xfer[0].speed_hz = spi_speed;
  xfer[0].bits_per_word = spi_bits;

  status = ioctl(fd, SPI_IOC_MESSAGE(1), xfer);
  if (status < 0)
  {
    pabort("SPI_IOC_MESSAGE");
  }

  return rx_buf[0];
}

/**
 * RFM69 default constructor. Use init() to start working with the RFM69 module.
 *
 * @param spi Pointer to a SPI device
 * @param csGPIO GPIO of /CS line (ie. GPIOA, GPIOB, ...)
 * @param csPin Pin of /CS line (eg. GPIO_Pin_1)
 * @param highPowerDevice Set to true, if this is a RFM69Hxx device (default: false)
 */
RFM69::RFM69(bool highPowerDevice)
{
  _init = false;
  _mode = RFM69_MODE_STANDBY;
  _highPowerDevice = highPowerDevice;
  _powerLevel = 0;
  _rssi = -127;
  _ookEnabled = false;
  _autoReadRSSI = true;
  _dataMode = RFM69_DATA_MODE_PACKET;
  _highPowerSettings = false;
  _csmaEnabled = false;
  _rxBufferLength = 0;

  _fd = open(device, O_RDWR);
  if (_fd < 0)
    pabort("Can't open device");

  int _ret = ioctl(_fd, SPI_IOC_WR_MODE, &spi_mode);
  if (_ret == -1)
    pabort("Can't set SPI mode");

  _ret = ioctl(_fd, SPI_IOC_RD_MODE, &spi_mode);
  if (_ret == -1)
    pabort("Can't set SPI mode");

  // Bits per word
  _ret = ioctl(_fd, SPI_IOC_WR_BITS_PER_WORD, &spi_bits);
  if (_ret == -1)
    pabort("Can't set bits per word");

  _ret = ioctl(_fd, SPI_IOC_RD_BITS_PER_WORD, &spi_bits);
  if (_ret == -1)
    pabort("Can't set bits per word");

  // Max speed hz
  _ret = ioctl(_fd, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed);
  if (_ret == -1)
    pabort("Can't set max speed hz");

  _ret = ioctl(_fd, SPI_IOC_RD_MAX_SPEED_HZ, &spi_speed);
  if (_ret == -1)
    pabort("Can't set max speed hz");

  printf("spi mode: %d\n", spi_mode);
  printf("bits per word: %d\n", spi_bits);
  printf("max speed: %d Hz (%d KHz)\n", spi_speed, spi_speed / 1000);

}

RFM69::~RFM69()
{
  close(_fd);
}

/**
 * Reset the RFM69 module using the external reset line.
 *
 * @note Use setResetPin() before calling this function.
 */
//void RFM69::reset()
//{
//  if (0 == _resetGPIO)
//    return;
//
//  _init = false;
//
//  // generate reset impulse
//  GPIO_SetBits(_resetGPIO, _resetPin);
//  HAL_Delay(1);
//  GPIO_ResetBits(_resetGPIO, _resetPin);
//
//  // wait until module is ready
//  HAL_Delay(10);
//
//  _mode = RFM69_MODE_STANDBY;
//}

/**
 * Initialize the RFM69 module.
 * A base configuration is set and the module is put in standby mode.
 *
 * @return Always true
 */
bool RFM69::init()
{
  // set base configuration
  setCustomConfig(rfm69_base_config, sizeof(rfm69_base_config) / 2);

  // set PA and OCP settings according to RF module (normal/high power)
  setPASettings();

  // clear FIFO and flags
  clearFIFO();

  _init = true;

  return _init;
}

/**
 * Set the carrier frequency in Hz.
 * After calling this function, the module is in standby mode.
 *
 * @param frequency Carrier frequency in Hz
 */
void RFM69::setFrequency(unsigned int frequency)
{
  // switch to standby if TX/RX was active
  if (RFM69_MODE_RX == _mode || RFM69_MODE_TX == _mode)
    setMode(RFM69_MODE_STANDBY);

  // calculate register value
  frequency /= RFM69_FSTEP;

  // set new frequency
  writeRegister(0x07, frequency >> 16);
  writeRegister(0x08, frequency >> 8);
  writeRegister(0x09, frequency);
}

/**
 * Set the FSK frequency deviation in Hz.
 * After calling this function, the module is in standby mode.
 *
 * @param frequency Frequency deviation in Hz
 */
void RFM69::setFrequencyDeviation(unsigned int frequency)
{
  // switch to standby if TX/RX was active
  if (RFM69_MODE_RX == _mode || RFM69_MODE_TX == _mode)
    setMode(RFM69_MODE_STANDBY);

  // calculate register value
  frequency /= RFM69_FSTEP;

  // set new frequency
  writeRegister(0x05, frequency >> 8);
  writeRegister(0x06, frequency);
}

/**
 * Set the bitrate in bits per second.
 * After calling this function, the module is in standby mode.
 *
 * @param bitrate Bitrate in bits per second
 */
void RFM69::setBitrate(unsigned int bitrate)
{
  // switch to standby if TX/RX was active
  if (RFM69_MODE_RX == _mode || RFM69_MODE_TX == _mode)
    setMode(RFM69_MODE_STANDBY);

  // calculate register value
  bitrate = RFM69_XO / bitrate;

  // set new bitrate
  writeRegister(0x03, bitrate >> 8);
  writeRegister(0x04, bitrate);
}

/**
 * Read a RFM69 register value.
 *
 * @param reg The register to be read
 * @return The value of the register
 */
uint8_t RFM69::readRegister(uint8_t reg)
{
  // sanity check
  if (reg > 0x7f)
    return 0;

  // read value from register
  chipSelect();

  uint16_t cmd = (reg << 8);
  uint8_t value = rf12_xferCmd(_fd, cmd);

  chipUnselect();

  return value;
}

/**
 * Write a RFM69 register value.
 *
 * @param reg The register to be written
 * @param value The value of the register to be set
 */
void RFM69::writeRegister(uint8_t reg, uint8_t value)
{
  // sanity check
  if (reg > 0x7f)
    return;

  // transfer value to register and set the write flag
  chipSelect();

  uint16_t cmd = ((reg | 0x80) << 8) | (((uint16_t)value) & 0xff);
  rf12_xferCmd(_fd, cmd);

  chipUnselect();
}

/**
 * Acquire the chip.
 */
void RFM69::chipSelect()
{
//  HAL_GPIO_WritePin(_csGPIO, _csPin, GPIO_PIN_RESET);
}

/**
 * Switch the mode of the RFM69 module.
 * Using this function you can manually select the RFM69 mode (sleep for example).
 *
 * This function also takes care of the special registers that need to be set when
 * the RFM69 module is a high power device (RFM69Hxx).
 *
 * This function is usually not needed because the library handles mode changes automatically.
 *
 * @param mode RFM69_MODE_SLEEP, RFM69_MODE_STANDBY, RFM69_MODE_FS, RFM69_MODE_TX, RFM69_MODE_RX
 * @return The new mode
 */
RFM69Mode RFM69::setMode(RFM69Mode mode)
{
  if ((mode == _mode) || (mode > RFM69_MODE_RX))
    return _mode;

  // set new mode
  writeRegister(0x01, mode << 2);

  // set special registers if this is a high power device (RFM69HW)
  if (true == _highPowerDevice)
  {
    switch (mode)
    {
    case RFM69_MODE_RX:
      // normal RX mode
      if (true == _highPowerSettings)
        setHighPowerSettings(false);
      break;

    case RFM69_MODE_TX:
      // +20dBm operation on PA_BOOST
      if (true == _highPowerSettings)
        setHighPowerSettings(true);
      break;

    default:
      break;
    }
  }

  _mode = mode;

  return _mode;
}

/**
 * Release the chip.
 */
void RFM69::chipUnselect()
{
//  HAL_GPIO_WritePin(_csGPIO, _csPin, GPIO_PIN_SET);
}

/**
 * Enable/disable the power amplifier(s) of the RFM69 module.
 *
 * PA0 for regular devices is enabled and PA1 is used for high power devices (default).
 *
 * @note Use this function if you want to manually override the PA settings.
 * @note PA0 can only be used with regular devices (not the high power ones!)
 * @note PA1 and PA2 can only be used with high power devices (not the regular ones!)
 *
 * @param forcePA If this is 0, default values are used. Otherwise, PA settings are forced.
 *                0x01 for PA0, 0x02 for PA1, 0x04 for PA2, 0x08 for +20 dBm high power settings.
 */
void RFM69::setPASettings(uint8_t forcePA)
{
  // disable OCP for high power devices, enable otherwise
  writeRegister(0x13, 0x0A | (_highPowerDevice ? 0x00 : 0x10));

  if (0 == forcePA)
  {
    if (true == _highPowerDevice)
    {
      // enable PA1 only
      writeRegister(0x11, (readRegister(0x11) & 0x1F) | 0x40);
    }
    else
    {
      // enable PA0 only
      writeRegister(0x11, (readRegister(0x11) & 0x1F) | 0x80);
    }
  }
  else
  {
    // PA settings forced
    uint8_t pa = 0;

    if (forcePA & 0x01)
      pa |= 0x80;

    if (forcePA & 0x02)
      pa |= 0x40;

    if (forcePA & 0x04)
      pa |= 0x20;

    // check if high power settings are forced
    _highPowerSettings = (forcePA & 0x08) ? true : false;
    setHighPowerSettings(_highPowerSettings);

    writeRegister(0x11, (readRegister(0x11) & 0x1F) | pa);
  }
}

/**
 * Set the output power level of the RFM69 module.
 *
 * @param power Power level from 0 to 31.
 */
void RFM69::setPowerLevel(uint8_t power)
{
  if (power > 31)
    power = 31;

  writeRegister(0x11, (readRegister(0x11) & 0xE0) | power);

  _powerLevel = power;
}

/**
 * Enable the +20 dBm high power settings of RFM69Hxx modules.
 *
 * @note Enabling only works with high power devices.
 *
 * @param enable true or false
 */
void RFM69::setHighPowerSettings(bool enable)
{
  // enabling only works if this is a high power device
  if (true == enable && false == _highPowerDevice)
    enable = false;

  writeRegister(0x5A, enable ? 0x5D : 0x55);
  writeRegister(0x5C, enable ? 0x7C : 0x70);
}

/**
 * Reconfigure the RFM69 module by writing multiple registers at once.
 *
 * @param config Array of register/value tuples
 * @param length Number of elements in config array
 */
void RFM69::setCustomConfig(const uint8_t config[][2], unsigned int length)
{
  for (unsigned int i = 0; i < length; i++)
  {
    writeRegister(config[i][0], config[i][1]);
  }
}

uint32_t HAL_GetTick()
{
  struct timespec spec;
  clock_gettime(CLOCK_REALTIME, &spec);
  return (spec.tv_sec + (spec.tv_nsec / 1000000));
}

/**
 * Send a packet over the air.
 *
 * After sending the packet, the module goes to standby mode.
 * CSMA/CA is used before sending if enabled by function setCSMA() (default: off).
 *
 * @note A maximum amount of RFM69_MAX_PAYLOAD bytes can be sent.
 * @note This function blocks until packet has been sent.
 *
 * @param data Pointer to buffer with data
 * @param dataLength Size of buffer
 *
 * @return Number of bytes that have been sent
 */
int RFM69::send(const void* data, unsigned int dataLength)
{
  // switch to standby and wait for mode ready, if not in sleep mode
  if (RFM69_MODE_SLEEP != _mode)
  {
    setMode(RFM69_MODE_STANDBY);
    waitForModeReady();
  }

  // clear FIFO to remove old data and clear flags
  clearFIFO();

  // limit max payload
  if (dataLength > RFM69_MAX_PAYLOAD)
    dataLength = RFM69_MAX_PAYLOAD;

  // payload must be available
  if (0 == dataLength)
    return 0;

  /* Wait for a free channel, if CSMA/CA algorithm is enabled.
   * This takes around 1,4 ms to finish if channel is free */
  if (true == _csmaEnabled)
  {
    // Restart RX
    writeRegister(0x3D, (readRegister(0x3D) & 0xFB) | 0x20);

    // switch to RX mode
    setMode(RFM69_MODE_RX);

    // wait until RSSI sampling is done; otherwise, 0xFF (-127 dBm) is read

    // RSSI sampling phase takes ~960 µs after switch from standby to RX
    uint32_t timeEntry = HAL_GetTick();
    while (((readRegister(0x23) & 0x02) == 0) && ((HAL_GetTick() - timeEntry) < 10));

    while ((false == channelFree()) && ((HAL_GetTick() - timeEntry) < TIMEOUT_CSMA_READY))
    {
      // wait for a random time before checking again
      delay(rand() % 10);

      /* try to receive packets while waiting for a free channel
       * and put them into a temporary buffer */
      int bytesRead;
      if ((bytesRead = _receive(_rxBuffer, RFM69_MAX_PAYLOAD)) > 0)
      {
        _rxBufferLength = bytesRead;

        // module is in RX mode again

        // Restart RX and wait until RSSI sampling is done
        writeRegister(0x3D, (readRegister(0x3D) & 0xFB) | 0x20);
        uint32_t timeEntry = HAL_GetTick();
        while (((readRegister(0x23) & 0x02) == 0) && ((HAL_GetTick() - timeEntry) < 10));
      }
    }

    setMode(RFM69_MODE_STANDBY);
  }

  // transfer packet to FIFO
  chipSelect();

  rf12_xferByte(_fd, 0x00 | 0x80);
  rf12_xferByte(_fd, dataLength);

  // send payload
  for (unsigned int i = 0; i < dataLength; i++)
    rf12_xferByte(_fd, ((uint8_t*)data)[i]);

  chipUnselect();

  // start radio transmission
  setMode(RFM69_MODE_TX);

  // wait for packet sent
  waitForPacketSent();

  // go to standby
  setMode(RFM69_MODE_STANDBY);

  return dataLength;
}

/**
 * Clear FIFO and flags of RFM69 module.
 */
void RFM69::clearFIFO()
{
  // clear flags and FIFO
  writeRegister(0x28, 0x10);
}

/**
 * Wait until the requested mode is available or timeout.
 */
void RFM69::waitForModeReady()
{
  uint32_t timeEntry = HAL_GetTick();

  while (((readRegister(0x27) & 0x80) == 0) && ((HAL_GetTick() - timeEntry) < TIMEOUT_MODE_READY));
}

/**
 * Put the RFM69 module to sleep (lowest power consumption).
 */
void RFM69::sleep()
{
  setMode(RFM69_MODE_SLEEP);
}

/**
 * Put the RFM69 module in RX mode and try to receive a packet.
 *
 * @note The module resides in RX mode.
 *
 * @param data Pointer to a receiving buffer
 * @param dataLength Maximum size of buffer
 * @return Number of received bytes; 0 if no payload is available.
 */
int RFM69::receive(unsigned char* data, unsigned int dataLength)
{
  // check if there is a packet in the internal buffer and copy it
  if (_rxBufferLength > 0)
  {
    // copy only until dataLength, even if packet in local buffer is actually larger
    memcpy(data, _rxBuffer, dataLength);

    unsigned int bytesRead = _rxBufferLength;

    // empty local buffer
    _rxBufferLength = 0;

    return bytesRead;
  }
  else
  {
    // regular receive
    return _receive(data, dataLength);
  }
}

/**
 * Put the RFM69 module in RX mode and try to receive a packet.
 *
 * @note This is an internal function.
 * @note The module resides in RX mode.
 *
 * @param data Pointer to a receiving buffer
 * @param dataLength Maximum size of buffer
 * @return Number of received bytes; 0 if no payload is available.
 */
int RFM69::_receive(unsigned char* data, unsigned int dataLength)
{
  // go to RX mode if not already in this mode
  if (RFM69_MODE_RX != _mode)
  {
    setMode(RFM69_MODE_RX);
    waitForModeReady();
  }

  uint8_t r;
  r = readRegister(0x24);
  uint8_t r2 = readRegister(0x27);
  if ((r < 0xc0) || (r2 & 0x07))
  {
    printf("0x24: %x 0x27:%x\r\n", r, r2);
  }


  r = readRegister(0x28);
//  if (r)  printf("0x28: %x\r\n", r);
  if (r & 0x04)
  {
    // go to standby before reading data
    setMode(RFM69_MODE_STANDBY);

    // get FIFO content
    unsigned int bytesRead = 0;

    // read until FIFO is empty or buffer length exceeded
    while ((readRegister(0x28) & 0x40) && (bytesRead < dataLength))
    {
      // read next byte
      data[bytesRead] = readRegister(0x00);
      printf("%x ", data[bytesRead]);
      bytesRead++;
    }

    printf("\r\n");
    // automatically read RSSI if requested
    if (true == _autoReadRSSI)
    {
      readRSSI();
      printf("rssi: %d\r\n", _rssi);
    }

    // go back to RX mode
    setMode(RFM69_MODE_RX);
    writeRegister(0x3D, readRegister (0x3D) | 0x04 );

    // todo: wait needed?
    //    waitForModeReady();

    return bytesRead;
  }
  else
    return 0;
}

/**
 * Enable and set or disable AES hardware encryption/decryption.
 *
 * The AES encryption module will be disabled if an invalid key or key length
 * is passed to this function (aesKey = 0 or keyLength != 16).
 * Otherwise encryption will be enabled.
 *
 * The key is stored as MSB first in the RF module.
 *
 * @param aesKey Pointer to a buffer with the 16 byte AES key
 * @param keyLength Number of bytes in buffer aesKey; must be 16 bytes
 * @return State of encryption module (false = disabled; true = enabled)
 */
bool RFM69::setAESEncryption(const void* aesKey, unsigned int keyLength)
{
  bool enable = false;

  // check if encryption shall be enabled or disabled
  if ((0 != aesKey) && (16 == keyLength))
    enable = true;

  // switch to standby
  setMode(RFM69_MODE_STANDBY);

  if (true == enable)
  {
    // transfer AES key to AES key register
    chipSelect();

    // address first AES MSB register
    rf12_xferByte(_fd, 0x3E | 0x80);

    // transfer key (0x3E..0x4D)
    for (unsigned int i = 0; i < keyLength; i++)
      rf12_xferByte(_fd, ((uint8_t*)aesKey)[i]);

    chipUnselect();
  }

  // set/reset AesOn Bit in packet config
  writeRegister(0x3D, (readRegister(0x3D) & 0xFE) | (enable ? 1 : 0));

  return enable;
}

/**
 * Wait until packet has been sent over the air or timeout.
 */
void RFM69::waitForPacketSent()
{
  uint32_t timeEntry = HAL_GetTick();

  while (((readRegister(0x28) & 0x08) == 0) && ((HAL_GetTick() - timeEntry) < TIMEOUT_PACKET_SENT));
}

/**
 * Read the last RSSI value.
 *
 * @note Only if the last RSSI value was above the RSSI threshold, a sample can be read.
 *       Otherwise, you always get -127 dBm. Be also careful if you just switched to RX mode.
 *       You may have to wait until a RSSI sample is available.
 *
 * @return RSSI value in dBm.
 */
int RFM69::readRSSI()
{
  _rssi = -readRegister(0x24) / 2;

  return _rssi;
}

/**
 * Debug function to dump all RFM69 registers.
 *
 * Symbol 'DEBUG' has to be defined.
 */
void RFM69::dumpRegisters(void)
{
#ifdef DEBUG
  for (unsigned int i = 1; i <= 0x71; i++)
  {
    printf("[0x%X]: 0x%X\n", i, readRegister(i));
  }
#endif
}

/**
 * Enable/disable OOK modulation (On-Off-Keying).
 *
 * Default modulation is FSK.
 * The module is switched to standby mode if RX or TX was active.
 *
 * @param enable true or false
 */
void RFM69::setOOKMode(bool enable)
{
  // switch to standby if TX/RX was active
  if (RFM69_MODE_RX == _mode || RFM69_MODE_TX == _mode)
    setMode(RFM69_MODE_STANDBY);

  if (false == enable)
  {
    // FSK
    writeRegister(0x02, (readRegister(0x02) & 0xE7));
  }
  else
  {
    // OOK
    writeRegister(0x02, (readRegister(0x02) & 0xE7) | 0x08);
  }

  _ookEnabled = enable;
}

/**
 * Configure the data mode of the RFM69 module.
 *
 * Default data mode is 'packet'. You can choose between 'packet',
 * 'continuous with clock recovery', 'continuous without clock recovery'.
 *
 * The module is switched to standby mode if RX or TX was active.
 *
 * @param dataMode RFM69_DATA_MODE_PACKET, RFM69_DATA_MODE_CONTINUOUS_WITH_SYNC, RFM69_DATA_MODE_CONTINUOUS_WITHOUT_SYNC
 */
void RFM69::setDataMode(RFM69DataMode dataMode)
{
  // switch to standby if TX/RX was active
  if (RFM69_MODE_RX == _mode || RFM69_MODE_TX == _mode)
    setMode(RFM69_MODE_STANDBY);

  switch (dataMode)
  {
  case RFM69_DATA_MODE_PACKET:
    writeRegister(0x02, (readRegister(0x02) & 0x1F));
    break;

  default:
    return;
  }

  _dataMode = dataMode;
}

/**
 * Set the output power level in dBm.
 *
 * This function takes care of the different PA settings of the modules.
 * Depending on the requested power output setting and the available module,
 * PA0, PA1 or PA1+PA2 is enabled.
 *
 * @param dBm Output power in dBm
 * @return 0 if dBm valid; else -1.
 */
int RFM69::setPowerDBm(int8_t dBm)
{
  /* Output power of module is from -18 dBm to +13 dBm
   * in "low" power devices, -2 dBm to +20 dBm in high power devices */
  if (dBm < -18 || dBm > 20)
    return -1;

  if (false == _highPowerDevice && dBm > 13)
    return -1;

  if (true == _highPowerDevice && dBm < -2)
    return -1;

  uint8_t powerLevel = 0;

  if (false == _highPowerDevice)
  {
    // only PA0 can be used
    powerLevel = dBm + 18;

    // enable PA0 only
    writeRegister(0x11, 0x80 | powerLevel);
  }
  else
  {
    if (dBm >= -2 && dBm <= 13)
    {
      // use PA1 on pin PA_BOOST
      powerLevel = dBm + 18;

      // enable PA1 only
      writeRegister(0x11, 0x40 | powerLevel);

      // disable high power settings
      _highPowerSettings = false;
      setHighPowerSettings(_highPowerSettings);
    }
    else if (dBm > 13 && dBm <= 17)
    {
      // use PA1 and PA2 combined on pin PA_BOOST
      powerLevel = dBm + 14;

      // enable PA1+PA2
      writeRegister(0x11, 0x60 | powerLevel);

      // disable high power settings
      _highPowerSettings = false;
      setHighPowerSettings(_highPowerSettings);
    }
    else
    {
      // output power from 18 dBm to 20 dBm, use PA1+PA2 with high power settings
      powerLevel = dBm + 11;

      // enable PA1+PA2
      writeRegister(0x11, 0x60 | powerLevel);

      // enable high power settings
      _highPowerSettings = true;
      setHighPowerSettings(_highPowerSettings);
    }
  }

  return 0;
}

/**
 * Check if the channel is free using RSSI measurements.
 *
 * This function is part of the CSMA/CA algorithm.
 *
 * @return true = channel free; otherwise false.
 */
bool RFM69::channelFree()
{
  if (readRSSI() < CSMA_RSSI_THRESHOLD)
  {
    return true;
  }
  else
  {
    return false;
  }
}

/** @}
 *
 */
