/**
 * @file rfm69.hpp
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
 * You have to provide your own functions for delay_ms and mstimer_get.
 * Use the SysTick timer (for example) with a 1 ms resolution which is present on all ARM controllers.
 *
 * If you want to port this library to other devices, you have to provide an SPI instance
 * derived from the SPIBase class.
 */

#ifndef RFM69_HXX_
#define RFM69_HXX_

/** @addtogroup RFM69
 * @{
 */
#define RFM69_MAX_PAYLOAD   64 ///< Maximum bytes payload

/**
 * Valid RFM69 operation modes.
 */
typedef enum
{
  RFM69_MODE_SLEEP = 0,//!< Sleep mode (lowest power consumption)
  RFM69_MODE_STANDBY,  //!< Standby mode
  RFM69_MODE_FS,       //!< Frequency synthesizer enabled
  RFM69_MODE_TX,       //!< TX mode (carrier active)
  RFM69_MODE_RX        //!< RX mode
} RFM69Mode;

/**
 * Valid RFM69 data modes.
 */
typedef enum
{
  RFM69_DATA_MODE_PACKET = 0,                 //!< Packet engine active
} RFM69DataMode;

static void pabort(const char *s);

/** RFM69 driver library for STM32 controllers. */
class RFM69
{
  /** @addtogroup RFM69
   * @{
   */
public:
  RFM69(bool highPowerDevice = false);
  virtual ~RFM69();

  void reset();

  bool init();

  void setFrequency(unsigned int frequency);

  void setFrequencyDeviation(unsigned int frequency);

  void setBitrate(unsigned int bitrate);

  RFM69Mode setMode(RFM69Mode mode);

  void setPowerLevel(uint8_t power);

  int setPowerDBm(int8_t dBm);

  void setHighPowerSettings(bool enable);

  void setCustomConfig(const uint8_t config[][2], unsigned int length);

  int send(const void* data, unsigned int dataLength);

  int receive(unsigned char* data, unsigned int dataLength);

  void sleep();

  /**
   * Gets the last "cached" RSSI reading.
   *
   * @note This only gets the latest reading that was requested by readRSSI().
   *
   * @return RSSI value in dBm.
   */
  int getRSSI()
  {
    return _rssi;
  }

  void setOOKMode(bool enable);

  void setDataMode(RFM69DataMode dataMode = RFM69_DATA_MODE_PACKET);

  /**
   * Enable/disable the automatic reading of the RSSI value during packet reception.
   *
   * Default is off (no reading).
   *
   * @param enable true or false
   */
  void setAutoReadRSSI(bool enable)
  {
    _autoReadRSSI = enable;
  }

  /**
   * Enable/disable the CSMA/CA (carrier sense) algorithm before sending a packet.
   *
   * @param enable true or false
   */
  void setCSMA(bool enable)
  {
    _csmaEnabled = enable;
  }

  void dumpRegisters();

  void setPASettings(uint8_t forcePA = 0);

  bool setAESEncryption(const void* aesKey, unsigned int keyLength);

private:
  uint8_t readRegister(uint8_t reg);

  void writeRegister(uint8_t reg, uint8_t value);

  void chipSelect();

  void chipUnselect();

  void clearFIFO();

  void waitForModeReady();

  void waitForPacketSent();

  int readRSSI();

  bool channelFree();

  int _receive(unsigned char* data, unsigned int dataLength);

  bool _init;
  RFM69Mode _mode;
  bool _highPowerDevice;
  uint8_t _powerLevel;
  int _rssi;
  bool _autoReadRSSI;
  bool _ookEnabled;
  RFM69DataMode _dataMode;
  bool _highPowerSettings;
  bool _csmaEnabled;
  char _rxBuffer[RFM69_MAX_PAYLOAD];
  unsigned int _rxBufferLength;
  int _fd;

  /** @}
   *
   */
};

#endif /* RFM69_HXX_ */

/** @}
 *
 */
