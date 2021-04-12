/*
 * DS18B20 source file
 */
 
/* 2021 Michael Nielson
 * Adapted for STM8S005 processor, ENC28J60 Ethernet Controller,
 * Web_Relay_Con V2.0 HW-584, and compilation with Cosmic tool set.
 * Author: Michael Nielson
 * Email: nielsonm.projects@gmail.com
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 See GNU General Public License at <http://www.gnu.org/licenses/>.
 
 Copyright 2021 Michael Nielson
*/


#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iostm8s005.h"
#include "stm8s-005.h"
#include "DS18B20.h"
#include "main.h"
#include "timer.h"
#include "uart.h"
#include "uipopt.h"


extern uint8_t DS18B20_scratch_byte[2]; // Array to store scratchpad bytes
                                        // read from DS18B20
extern uint8_t OctetArray[11];		// Used in conversion of integer
                                        // values to character values and
					// used to store generic strings

// Table used for rounding the decimal part of temperatures
static const uint8_t dec_temp[] = {
  '0',  // 0x0000 0.0000 rounded off = 0.0
  '1',  // 0x0001 0.0625 rounded off = 0.1
  '1',  // 0x0010 0.1250 rounded off = 0.1
  '2',  // 0x0011 0.1875 rounded off = 0.2
  '3',  // 0x0100 0.2500 rounded off = 0.3
  '3',  // 0x0101 0.3125 rounded off = 0.3
  '4',  // 0x0110 0.3750 rounded off = 0.4
  '4',  // 0x0111 0.4375 rounded off = 0.4
  '5',  // 0x1000 0.5000 rounded off = 0.5
  '6',  // 0x1001 0.5625 rounded off = 0.6
  '6',  // 0x1010 0.6250 rounded off = 0.6
  '7',  // 0x1011 0.6875 rounded off = 0.7
  '8',  // 0x1100 0.7500 rounded off = 0.8
  '8',  // 0x1101 0.8125 rounded off = 0.8
  '9',  // 0x1110 0.8750 rounded off = 0.9
  '9'}; // 0x1111 0.9375 rounded off = 0.9

// GLOBAL VARIABLES FOR MAXIM DS18B20 CODE CONTRIBUTION
// Derived from Maxim code
// https://www.maximintegrated.com/en/design/technical-documents/app-notes/1/162.html
uint8_t ROM[8];          // ROM bytes
uint8_t lastDiscrep = 0; // last discrepancy
uint8_t doneFlag = 0;    // Done flag
uint8_t FoundROM[5][8];  // Table of found ROM codes
int numROMs;             // Count of DS18B20 devices found





//---------------------------------------------------------------------------//
// This function uses IO 16 to operate up to 5 DS18B20 devices.
//
// If the DS18B20 Feature is enabled in the Configuration page IO 16
// is kept "disabled" from the perspective of normal input/output operation.
// This leaves the IO pin free to be manipulated by this code for DS18B20 IO.
//
// In main.c, the write_output_pins() function will not write IO 16.
// Instead in main.c, the check_runtime_changes() function will set IO 16
// to disabled if the Feature "DS18B20" is enabled. This removes the IO pin
// from the IOControl page, and the IO pin will be kept disabled even if the
// user attempts to enable them in the Configuration page.
//
// Hardware requirements:
// a) You must add a 4.7Kohm pull up to 3V or to 5V.
// b) The DS18B20 must have a local 3V or 5V power supply to match the
//    voltage on the pull up resistor.
//---------------------------------------------------------------------------//


void get_temperature()
{
  // This function will be called by main every 30 seconds. The function
  // reads the current temperature from all devices, then signals the
  // DS18B20s to start a new conversion so that a new temperature reading
  // will be ready the next time this function is called. Then the function
  // returns leaving the temperature values in the DS18B20_scratch array.
  //
  // Note that the first time the temperature is read from the DS18B20s the
  // value will be indeterminate, but on the next read the value will be
  // correct. When the Network Module powers up it should run this routine
  // once to initialize the DS18B20 but should disregard the first value read.
  //
  // The communication steps are:
  //   Reset pulse sent to all DS18B20s
  //   Reset Ack pulse sent from the DS18B20s
  //   Match ROM command sent to the DS18B20 of interest
  //   Read Scratchpad command sent to the DS18B20 of interest
  //   16 bits (first 2 bytes) of Scratchpad data are requested from (and sent
  //     by) the DS18B20 of interest
  //   Reset pulse sent to all DS18B20s
  //   Reset Ack pulse sent from the DS18B20s
  //   Match ROM command sent to the DS18B20 of interest
  //   Convert Temp command sent to the DS18B20 of interest
  // These steps are then repeated for the next DS18B20
  //
  // Detailed timing note: Below a for() loop is used for the smaller timing
  // intervals similar to this:
  //   for (nop_cnt=0; nop_cnt<4; nop_cnt++) nop();
  // Test measurements have shown that the "nop_cnt<xxx" part of the loop can
  // fairly accurately determine the wait time interval. For a given number
  // of microseconds of delay needed the formula is #us = limit/2. Thus,
  //   "nop_cnt<2" yields a 1us delay
  //   "nop_cnt<4" yields a 2us delay
  //   "nop_cnt<10" yields a 5us delay
  //   "nop_cnt<20" yields a 10us delay
  //   The delay is actually about 95% of the calculated value, but this is
  //   close enough for this application.
  
  int i;
  uint8_t j;
  uint8_t device_num;
  extern uint8_t DS18B20_scratch[5][2];

  // Read current temperature from up to 5 devices
  for (device_num = 0; device_num < 5; device_num++) {
    // Attempt to read up to 5 devices. If no devices are present the
    // reset_pulse() function will return 1. The value num_ROMs is the last
    // index for devices found in the FindDevices() function. num_ROMs == -1
    // indicates no devices.
    if (reset_pulse()) return; // If reset_pulse returns 1 no devices are
                               // present
    
    if (device_num <= numROMs) {
    
      transmit_byte(0x55); // match_ROM command. match_ROM must be followed
      // by sending the 8 bytes of the ROM contents previously read in the
      // search_ROM process, starting with bit 0 of byte 0.
      for (i = 0; i < 8; i++) transmit_byte(FoundROM[device_num][i]);
      
      // After the match_ROM command only the addressed DS18B20 will respond
      // to subsequent commands until another reset_pulse() occurs.
      
      // Next step is to read the temperature data from the scratch_pad.
      transmit_byte(0xbe); // read_scratchpad command
      // After the "read_scratchpad command" is sent the DS18B20 is enabled
      // to send up to 8 bytes of the scratchpad followed by the CRC byte.
      // Actual send from the DS18B20 is triggered by the Master (the Network
      // Module) sending a ~1us low pulse for each bit to be sent by the
      // DS18B20.
      // The definition of the scratchpad is as follows, HOWEVER we are only
      // going to read the first two bytes.
      // The scratchpad bytes are received starting with Byte 0 Bit 0.
      //   Byte 0: Temperature LSB
      //   Byte 1: Temperature MSB
      //   Byte 2: Th register
      //   Byte 3: Tl register
      //   Byte 4: Configuration register
      //   Byte 5: Reserved
      //   Byte 6: Reserved
      //   Byte 7: Reserved
      //   Byte 9: CRC
      //
      // Approximate time spent in this routine if all bytes are read(mostly
      // wait time):
      //   Send 8 bit read scratchpad command - about 75us per bit
      //   Read 72 bit scratchpad -  about 75us per bit
      //   80 x 75us = 6000us = 6ms
      // Approximate time spent in this routine when only the first 2 bytes
      // are read:
      //   Send 8 bit read scratchpad command - about 75us per bit
      //   Read 16 bit scratchpad -  about 75us per bit
      //   24 x 75us = 1800us = 1.8ms
      
      for (i=0; i<2; i++) {
        j = 0x01;
        while(1) {
          if (read_bit() == 1) DS18B20_scratch[device_num][i] |= j;
          else DS18B20_scratch[device_num][i] &= (uint8_t)~j;
          if (j == 0x80) break;
          j = (uint8_t)(j << 1);
        }
      }
      
      // Start new conversion
      reset_pulse();
      transmit_byte(0x55); // match_ROM command. match_ROM must be followed
      // by sending the 8 bytes of the ROM contents previously read in the
      // search_ROM process, starting with bit 0 of byte 0.
      for (i = 0; i < 8; i++) transmit_byte(FoundROM[device_num][i]);
      transmit_byte(0x44); // convert_temp command
    }
  }
}


void convert_temperature(uint8_t device_num, uint8_t degCorF)
{
//---------------------------------------------------------------------------//
  // This function will convert a temperature value stored in the
  // DS18B20_scratch array into a string in degrees C or degrees F. The
  // function leaves the converted result in the global OctetArray string.
  // 
  int16_t whole_temp;
  uint8_t decimal_temp;
  extern uint8_t DS18B20_scratch[5][2];
  uint8_t temp_string[7];
  
  // Convert temperature reading to string.
  // DS18B20_temp_xx is a 16 bit signed value. Bits are organized as
  // follows:
  //   Bits 15,14,13,12,11 are all the sign bit
  //   Bits 10,9,8,7,6,5,4 are the digits of the temperature whole number
  //     If positive value these bits can be converted to the decimal
  //     whole number
  //     If negative value these bits should be inverted, then converted
  //     to the decimal whole number and an "-" should be added
  //   Bits 3,2,1,0 are the decimal part of the temperature
  //     If positive value and a single decimal is wanted the round-off
  //     table declared in this file applies (static const uint8_t dec_temp[]).
  //     If negative the four "decimal" bits need to be inverted, then 1
  //     added (twos complement). The round-off table then applies.
  
  // Test tables
  // These can be enabled one set at a time to test any code changes made
  // to the C and F conversion code. If enabled these test values will
  // over-ride any values read from the temperature sensors.
/*
  DS18B20_scratch[0][1] = 0x07; // +125.0000C +257.0F
  DS18B20_scratch[0][0] = 0xd0;
  DS18B20_scratch[1][1] = 0x05; // +85.0000C  +185.0F
  DS18B20_scratch[1][0] = 0x50;
  DS18B20_scratch[2][1] = 0x01; // +25.0625C  +77.1F
  DS18B20_scratch[2][0] = 0x91;
  DS18B20_scratch[3][1] = 0x00; // +10.1250C  +50.2F
  DS18B20_scratch[3][0] = 0xa2;
  DS18B20_scratch[4][1] = 0x00; // +0.5000C   +32.9F
  DS18B20_scratch[4][0] = 0x08;

  DS18B20_scratch[0][1] = 0x00; // +0.0000C   +32.0F
  DS18B20_scratch[0][0] = 0x00;
  DS18B20_scratch[1][1] = 0xff; // -0.5000C   +31.1F
  DS18B20_scratch[1][0] = 0xf8;
  DS18B20_scratch[2][1] = 0xff; // -10.1250C  +13.8F
  DS18B20_scratch[2][0] = 0x5e;
  DS18B20_scratch[3][1] = 0xfe; // -25.0625C  -13.1F
  DS18B20_scratch[3][0] = 0x6f;
  DS18B20_scratch[4][1] = 0xfc; // -55.0000C  -67.0F
  DS18B20_scratch[4][0] = 0x90;

  DS18B20_scratch[0][1] = 0x00; // +1.0000C   +33.8F
  DS18B20_scratch[0][0] = 0x10;
  DS18B20_scratch[1][1] = 0x00; // +1.0625C   +33.9F
  DS18B20_scratch[1][0] = 0x11;
  DS18B20_scratch[2][1] = 0x00; // +1.1250C   +34.0F
  DS18B20_scratch[2][0] = 0x12;
  DS18B20_scratch[3][1] = 0x00; // +1.1875C   +34.1F
  DS18B20_scratch[3][0] = 0x13;
  DS18B20_scratch[4][1] = 0x00; // +1.2500C   +34.3F
  DS18B20_scratch[4][0] = 0x14;

  DS18B20_scratch[0][1] = 0x00; // +1.6875C   +35.0F
  DS18B20_scratch[0][0] = 0x1b;
  DS18B20_scratch[1][1] = 0x00; // +1.7500C   +35.2F
  DS18B20_scratch[1][0] = 0x1c;
  DS18B20_scratch[2][1] = 0x00; // +1.8125C   +35.3F
  DS18B20_scratch[2][0] = 0x1d;
  DS18B20_scratch[3][1] = 0x00; // +1.8750C   +35.4F
  DS18B20_scratch[3][0] = 0x1e;
  DS18B20_scratch[4][1] = 0x00; // +1.9375C   +35.5F
  DS18B20_scratch[4][0] = 0x1f;

  DS18B20_scratch[0][1] = 0xff; // -1.0000C   +30.2F
  DS18B20_scratch[0][0] = 0xf0;
  DS18B20_scratch[1][1] = 0xff; // -1.0625C   +30.1F
  DS18B20_scratch[1][0] = 0xef;
  DS18B20_scratch[2][1] = 0xff; // -1.1250C   +30.0F
  DS18B20_scratch[2][0] = 0xee;
  DS18B20_scratch[3][1] = 0xff; // -1.1875C   +29.9F
  DS18B20_scratch[3][0] = 0xed;
  DS18B20_scratch[4][1] = 0xff; // -1.2500C   +29.8F
  DS18B20_scratch[4][0] = 0xec;

  DS18B20_scratch[0][1] = 0xff; // -1.6875C   +29.0F
  DS18B20_scratch[0][0] = 0xe5;
  DS18B20_scratch[1][1] = 0xff; // -1.7500C   +28.9F
  DS18B20_scratch[1][0] = 0xe4;
  DS18B20_scratch[2][1] = 0xff; // -1.8125C   +28.7F
  DS18B20_scratch[2][0] = 0xe3;
  DS18B20_scratch[3][1] = 0xff; // -1.8750C   +28.6F
  DS18B20_scratch[3][0] = 0xe2;
  DS18B20_scratch[4][1] = 0xff; // -1.9375C   +28.5F
  DS18B20_scratch[4][0] = 0xe1;

  DS18B20_scratch[0][1] = 0xfe; // -17.6875C  +00.2F
  DS18B20_scratch[0][0] = 0xe5;
  DS18B20_scratch[1][1] = 0xfe; // -17.7500C  +00.1F
  DS18B20_scratch[1][0] = 0xe4;
  DS18B20_scratch[2][1] = 0xfe; // -17.8125C  -00.1F
  DS18B20_scratch[2][0] = 0xe3;
  DS18B20_scratch[3][1] = 0xfe; // -17.8750C  -00.2F
  DS18B20_scratch[3][0] = 0xe2;
  DS18B20_scratch[4][1] = 0xfe; // -17.9375C  -00.3F
  DS18B20_scratch[4][0] = 0xe1;

  DS18B20_scratch[0][1] = 0xfe; // -18.1875C  -00.7F
  DS18B20_scratch[0][0] = 0xdd;
  DS18B20_scratch[1][1] = 0xfe; // -18.2500C  -00.9F
  DS18B20_scratch[1][0] = 0xdc;
  DS18B20_scratch[2][1] = 0xfe; // -18.3125C  -01.0F
  DS18B20_scratch[2][0] = 0xdb;
  DS18B20_scratch[3][1] = 0xfe; // -18.3750C  -01.0F
  DS18B20_scratch[3][0] = 0xda;
  DS18B20_scratch[4][1] = 0xfe; // -18.4375C  -01.2F
  DS18B20_scratch[4][0] = 0xd9;
*/


  if (DS18B20_scratch[device_num][1] != 0x55) { // Check that sensor exists
  
    if (degCorF == 0) {
      // Convert to degrees C
      // Extract whole temp and decimal temp parts of the DS18B20
      // values
      whole_temp = DS18B20_scratch[device_num][1];
      whole_temp = whole_temp << 8;
      whole_temp |= DS18B20_scratch[device_num][0];
      whole_temp &= 0x07f0;
      whole_temp = whole_temp >> 4;
      decimal_temp = (uint8_t)DS18B20_scratch[device_num][0];
      decimal_temp &= 0x0f;
        
      if ((DS18B20_scratch[device_num][1] & 0x80) == 0x80) {
        // Negative number conversion. Convert to positive
	// number.
        whole_temp = whole_temp ^ 0x007f;
        decimal_temp = (uint8_t)(decimal_temp ^ 0x0f);
        decimal_temp++;
        decimal_temp = (uint8_t)(decimal_temp & 0x0f);
        temp_string[0] = '-';
      }
      else {
        // Positive number
        temp_string[0] = ' ';
      }
    }
    

    else {
      // Convert to degrees F
      // This routine avoids the use of float variables to perform
      // C to F conversion in order to reduce code size. The result
      // is that some conversions are off by 0.1 degree - but this
      // is adequate for this application.
      {
        uint16_t raw_temp;
        int16_t F_temp0;
        int32_t F_temp1;
        int32_t F_temp2;
        
        // Use the raw number from the DS18B20 including the decimal
        raw_temp = DS18B20_scratch[device_num][1];
        raw_temp = raw_temp << 8;
        raw_temp |= DS18B20_scratch[device_num][0];
	// Recast raw_temp for subsequent calculations. Why two steps
	// here? Recast of a uint16 to a int32 did not work properly,
	// but recast of the uint16 to int16, then recast of the int16
	// to the int32 DID work.
        F_temp0 = (int16_t)raw_temp;
        F_temp1 = (int32_t)F_temp0;
        // Add 55 C to the value so math is always positive
        // We actually add 55 * 16, or 880, since we are working
	// with the raw number which includes 4 bits of decimal
	// This next equation also includes the "9" part of the
	// "9 / 5" calculation. We use 180 / 100 to avoid loss
	// of precision in the integer arithmatic.
        F_temp1 = (int32_t)((F_temp1 + 880) * 180);
	// It is necessary to separate the "100" part of the
	// "180 / 100" arithmatic so the compiler doesn't optimize
	// and cause loss of precision.
        F_temp2 = F_temp1 / 100;
	// Now subtract 1072. This is the combination of the "+32"
	// part of the C = (F * 9 / 5) + 32 equation, plus the removal
	// of the 55 C offset. Again we are using values mulitplied
	// by 16 since we are working with the raw number.
	// +32 = 32 * 16 = +512
	// The 55 C offset must be removed in terms of degrees F
	// F = (-55 * 9 / 5) * 16 = -1584
	// + 512 - 1584 = -1072
        F_temp2 = F_temp2 - 1072;
        // Now divide by 16 to get the "whole number" part of the 
	// display in degrees F
        whole_temp = (int16_t)(F_temp2 / 16);
        // Now calculate the "decimal" part of the display in degrees F
        decimal_temp = (uint8_t)(F_temp2 & 0xf);
        temp_string[0] = ' ';
        if (F_temp2 < 0) {
	  // Must use twos complement if the result is negative
          whole_temp = whole_temp * -1;
          decimal_temp = (uint8_t)(((decimal_temp ^ 0xf) + 1) & 0x0f);
          temp_string[0] = '-';
        }
      }
    }

    // Build string
    emb_itoa(whole_temp, OctetArray, 10, 3);
    temp_string[1] = OctetArray[0];
    temp_string[2] = OctetArray[1];
    temp_string[3] = OctetArray[2];
    temp_string[4] = '.';
    temp_string[5] = dec_temp[decimal_temp];
    temp_string[6] = '\0';
    strcpy(OctetArray, temp_string);
  }
  else {
    // Sensor does not exist - return ------ string
    strcpy(OctetArray, "------");
  }
}


// IO 16 is Port C bit 6 (of 0-7)
//   PC_DDR 1 is output, 0 is input
//   PC_ODR
//   PC_IDR


int reset_pulse()
{
  // Generates a master reset pulse on the specified IO
  // The pulse must be a minimum of 480us
  int rtn;
  
  PC_ODR |= 0x40;           // write IO ODR to 1
  PC_DDR |= 0x40;           // write IO DDR to output
  PC_ODR &= (uint8_t)~0x40; // write IO ODR to 0
  wait_timer(500);          // wait 500us
  PC_DDR &= (uint8_t)~0x40; // write IO DDR to input
  wait_timer(100);          // wait 100us
  
  // Check for "presence" state on the 1-wire. 0 = device(s) present.
  rtn = 0;
  if (PC_IDR & 0x40) rtn = 1;

  wait_timer(200);          // wait 200us
  return rtn;
}


void transmit_byte(uint8_t transmit_value)
{
  uint8_t j;
  j = 0x01;
  
  // This function will transmit the byte provided in transmit_value one bit
  // at a time.
  // To send a 1 bit we need to pulse the output low for a minimum of 5us and
  //   and max of 15us. Then we release the pin and wait 60us before sending
  //   the next bit.
  // To send a 0 bit we need to pulse the ouptut low for a minimum of 60us
  //   and max of 120us. Using the wait_timer with a wait of 60us will
  //   accomplish this. After the output low period we must wait a minimum of
  //   15us before sending the next bit. To reduce code size we will wait
  //   60us.

  while ( 1 ) {
    if (j & transmit_value) write_bit(1); // Send a 1
    else                    write_bit(0); // Send a 0
    if (j == 0x80) break;
    j = (uint8_t)(j << 1);
  }
}


int read_bit()
{
  // Read timing is a little tricky written in C code (less tricky if written
  // in assembly).
  // The Master (the Network Module) must drive the pin low for 2us, then
  // read the state of the pin as close to 15us later as possible (at least
  // according to the spec). Actual measurement shows that a read as late as
  // 30us later will still be valid.
  // If the DS18B20 is trasnmitting a zero it will be present for 15us.
  // If the DS18B20 is transmitting a one we rely on the 4.7K pullup resistor
  // to pull the wire to a 1 in less than 15 us. Actual measurement shows the
  // pullup working in about 1/2us with a 12 inch wire lead. Longer leads to
  // the DS18B20 may result in a slower rise time.
  // After reading a bit we must wait 60us before reading the next bit.
  int nop_cnt;
  int bit;

  bit = 0;

  PC_ODR |= 0x40;            // write IO ODR to 1
  PC_DDR |= 0x40;            // write IO DDR to output
  PC_ODR &= (uint8_t)~0x40;  // write IO ODR to 0
  for (nop_cnt=0; nop_cnt<4; nop_cnt++) nop(); // Provides a 1us pulse
  PC_DDR &= (uint8_t)~0x40;  // write IO DDR to input
  for (nop_cnt=0; nop_cnt<30; nop_cnt++) nop(); // Wait 15us
  if (PC_IDR & 0x40) bit = 1;
  
  wait_timer(60); // This timer is not critical - but we must wait at
                  // least 60 us
  return bit;
}


void write_bit(uint8_t transmit_bit)
{
  int i;
  
  // To send a 1 bit we need to pulse the output low for a minimum of 5us and
  //   max of 15us. Then we release the pin and wait 60us before returning to
  //   the calling function.
  // To send a 0 bit we need to pulse the ouptut low for a minimum of 60us
  //   and max of 120us. Using the wait_timer with a wait of 60us will
  //   accomplish this. After the output low period we must wait a minimum of
  //   15us before returning to the calling routine. To reduce code size we
  //   will wait 60us.
  
  PC_ODR |= 0x40;              // write IO ODR to 1
  PC_DDR |= 0x40;              // write IO DDR to output
  PC_ODR &= (uint8_t)~0x40;    // write IO ODR to 0
  for (i=0; i<10; i++) nop();  // If sending a 1 just provide 5us low time
  if (!(transmit_bit)) wait_timer(60); // If sending a 0 provide additional
                                       // 60us low time
  PC_DDR &= (uint8_t)~0x40;    // write IO DDR to input - will float high
  
  wait_timer(60); // Wait 60us before returning. This is needed to provide a
                  // pause brfore sending the next bit.
}


void FindDevices(void)
{
  // FIND DEVICES
  // Derived from Maxim code
  // https://www.maximintegrated.com/en/design/technical-documents/app-notes/1/162.html
  //
  // When done all found devices will have their ROM contents stored in the
  // Found_ROM table, and numROMs will contain the index of the last device
  // found (a value equal to one less than the number of devices found since
  // the index is 0, 1, 2, 3, 4 for the devices).

  int m;
  
  numROMs = -1; // -1 indicates no devices
  
  if (!reset_pulse()) {  //Begins when a presence is detected
    if (First()) {       //Begins when at least one part is found
      do {
        numROMs++; // On first pass this increments numROMs to index 0
        for (m=0; m<8; m++) {
          FoundROM[numROMs][m] = ROM[m]; // Identifies family, serial number, and
	                                 // CRC on found device
        }
      } while (Next() && (numROMs < 5)); // Continues until no additional
                                         // devices are found
    }
  }
}


uint8_t First(void)
{
  // FIRST
  // The First function resets the current state of a ROM search and calls
  // Next to find the first device on the 1-Wire bus.
  // Derived from Maxim code
  // https://www.maximintegrated.com/en/design/technical-documents/app-notes/1/162.html
  //
  lastDiscrep = 0;   // reset the rom search last discrepancy global
  doneFlag = FALSE;
  return Next();     // call Next and return its return value
}


uint8_t Next(void)
{
  // NEXT
  // The Next function searches for the next device on the 1-Wire bus. If
  // there are no more devices on the 1-Wire then false is returned.
  // Derived from Maxim code
  // https://www.maximintegrated.com/en/design/technical-documents/app-notes/1/162.html
  //
  uint8_t m = 1; // ROM Bit index
  int n = 0; // ROM Byte index
  uint8_t k = 1; // bit mask
  int x = 0;
  uint8_t discrepMarker = 0; // discrepancy marker
  uint8_t g;     // Output bit
  uint8_t nxt;   // return value
  uint8_t crc;
  
  nxt = 0;       // set the next flag to false
  crc = 0;
  
  if (reset_pulse() || doneFlag) { // Reset the 1-wire, make sure there
                                   // are parts, and verify that we are
				   // not done yet.
    lastDiscrep = 0;       // Reset the search
    return 0;
  }
  
  transmit_byte(0xF0); // send SearchROM command
  
  do { // for all eight bytes
    x = 0;
    if (read_bit() == 1) x = 2;
    if (read_bit() == 1) x |= 1; // and its complement
    if (x == 3) break;           // there are no devices on the 1-Wire
    else {
      if (x > 0) g = (uint8_t)(x >> 1);  // all devices coupled have 0 or 1
                                         // bit write value for search
      else {
        // if this discrepancy is before the last
        // discrepancy on a previous Next then pick
        // the same as last time
        if(m < lastDiscrep) g = (uint8_t)((ROM[n] & k) > 0);
        else g = (uint8_t)(m == lastDiscrep);  // if equal to last pick 1
                                               // if not then pick 0
                                               // if 0 was picked then record
                                               // position with mask k
        if (g == 0) discrepMarker = m;
      }
      if (g == 1) ROM[n] |= k;  // isolate bit in ROM[n] with mask k
      else ROM[n] &= (uint8_t)~k;
      write_bit(g);             // ROM search write
      m++;                      // increment bit counter m
      k = (uint8_t)(k << 1);    // and shift the bit mask k
      if (k == 0) {             // if the mask is 0 then go to new ROM
                                //   byte n and reset mask
      n++; k++;
      }
    }
  } while(n < 8); //loop until through all ROM bytes 0-7
  
  // Calculate CRC for first 8 ROM bytes
  crc = dallas_crc8(ROM, 8);
  
  if (m < 65 || (crc != ROM[8])) lastDiscrep = 0;  // if search was
  // unsuccessful then reset the last discrepancy to 0
  else {
    // search was successful, so set lastDiscrep, lastOne, nxt
    lastDiscrep = discrepMarker;
    doneFlag = (uint8_t)(lastDiscrep == 0);
    nxt = 1; // indicates search is not complete yet, more parts remain
  }
  return nxt;
}


uint8_t dallas_crc8(uint8_t *data, uint8_t size)
{
    int i;
    int j;
    uint8_t inbyte;
    uint8_t mix;
    uint8_t crc;
    
    crc = 0;
    for ( i = 0; i < size; ++i )
    {
        inbyte = data[i];
        for ( j = 0; j < 8; ++j )
        {
            mix = (uint8_t)((crc ^ inbyte) & 0x01);
            crc >>= 1;
            if ( mix ) crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}
