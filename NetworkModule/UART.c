/*
 * UART source file
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

// Credit to Mark Stevens for the original concepts used here.
// https://blog.mark-stevens.co.uk/2012/08/using-the-uart-on-the-stm8s-2/


#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iostm8s005.h"
#include "stm8s-005.h"
#include "UART.h"
#include "main.h"
#include "timer.h"
#include "uipopt.h"



//---------------------------------------------------------------------------//
// This function enables the use of the STM8S UART to output characters to a
// terminal.

// The UART operates in asynchronous mode as the synchronous clock pin is
// also the ENC28J60 chip select pin. This cannot be changed.
//
// The UART tx and rx pins require use of the following IO pins:
//   IO 11 (tx)
//   IO 3 (rx)
// Because these pins are repurposed while the UART is used the UART function
// is only available is a debug build. When the debug build is generated the
// developer must be careful that these two IO pins do not have any circuitry
// connected that might cause damage to the IO pins. Further, because the
// pins operate at 3v a level converter is needed to connect the pins to
// external RS232 logic levels.

// Hardware requirements:
//---------------------------------------------------------------------------//




// Code allows the STM8S to send debug information to a terminal emulator
// running at 115200,n,8,1
//
// Perform the following tasks:
//   set the parity and number of data bits
//   set the parity
//   set the number of stop bits
//   setup the baud rate
//   set the clock polarity etc.

// The Registers
// It is important to remember that transmission and reception must both be
// disabled before we start to change these registers.
// UART_CR1 � Data Bits and Parity
//   The number of data bits is selected using the M bit of CR1. In this
//     application we will be using 8 data bits so UART_CR1_M is set to 0.
//   In this application parity is disabled so PCEN (Parity Control Enable)
//     is set to 0.
//
// UART_CR3 � Number of Stop Bits and Clock Settings
//   UART_CR3_STOP is set to a value of 00 for 1 stop bit.
//   UART_CR3_CPOL determines the idle state of the clock output. In this
//     application we don't use the clock output so this setting doesn't
//     matter.
//   UART_CR3_CPHA determines if the data should be stable on the rising or
//     falling edge of the output clock. In this application we don't use
//     the clock output so this setting doesn't matter.
//   UART_CR3_LBCL determines the clock pulse for the last data bit. In this
//     application we don't use the clock output so this setting doesn't
//     matter.
//   UART_BRR1 & UART_BRR2 � Baud Rate Registers
//     The baud rate of the UART is controlled by dividing fmaster by the
//     baud rate divisor. The result gives the clock speed of the serial port.
//     In this application we have a 16 MHz clock speed for fmaster.
//
//     The formula for 115200 baud:
//     UART Divider = fmaster / baud rate
//                  = 16,000,000 / 115,200
//                  = 138
//                  = 0x008a
//     Now we need to rearrange the number 0x008a in order to get the right
//     bits into BRR1 and BRR2 (Baud Rate Register 1 & 2). This was written
//     as a 32 bit number to illustrate how this is put into the registers.
//     To do this we split the number (represented by d3d2d1d0) into three
//     parts:
//     the first digit (d3) � 0
//     the next two digits (d2d1) � 08
//     the last digit (d0) � a
//     And set up the registers as follows:
//       BRR1 = d2d1
//            = 0x08
//       BRR2 = d3d0
//            = 0x0a
//       When setting these registers it is important to remember to set BRR2
//       before setting BRR1.
//
// 
//     For 9600 baud
//     UART Divider = fmaster / baud rate
//                  = 16,000,000 / 9600
//                  = 1666
//                  = 0x0682
//     Set up the registers as follows:
//       BRR1 = d2d1
//            = 0x68
//       BRR2 = d3d0
//            = 0x02
//
//   UART_CR2 & UART_CR3 � Enabling the UART
//       UART_CR2_TEN    Enable/disable transmission
//       UART_CR2_REN    Enable/disable reception
//       UART_CR3_CKEN   Enable/disable the clock
//     In this application we do not need to enable the clock output (used
//     for synchronous mode), and we do not need to enable receptio (as we
//     will only transmit characters).
//
// Setup the UART to run at 115200 baud, no parity, one stop bit, 8 data bits.
// Important: This relies upon the system clock being set to run at 16 MHz.
//
void InitializeUART(void)
{
  unsigned char tmp;

  //
  // Clear the Idle Line Detected bit in the status register by a read to the
  // UART2_SR register followed by a Read to the UART2_DR register.
  tmp = UART2_SR;
  tmp = UART2_DR;
  
// if (UART2_SR == 0xc0) fastflash(); // verfied that UART2_SR contains 0xc0
// fastflash();

  //  Reset the UART registers to the reset values.
  UART2_BRR2 = UART2_BRR2_RESET_VALUE;
  UART2_BRR1 = UART2_BRR1_RESET_VALUE;
  UART2_CR1 = UART2_CR1_RESET_VALUE;
  UART2_CR2 = UART2_CR2_RESET_VALUE;
  UART2_CR4 = UART2_CR3_RESET_VALUE;
  UART2_CR3 = UART2_CR4_RESET_VALUE;
  UART2_CR5 = UART2_CR5_RESET_VALUE;

  // All bits for 8 bit data, No Parity, 1 Stop are in the correct state due
  // to the Reset values above. We only need to set the baud rate and enable
  // the Transmitter.

  // Set the baud rate registers to 9600
//  UART2_BRR2 = 0x02;
//  UART2_BRR1 = 0x68;
  // Set the baud rate registers to 115200
  UART2_BRR2 = 0x0a;
  UART2_BRR1 = 0x08;
  
  // Set the Transmitter Enable bit
  UART2_CR2 |= (uint8_t)UART2_CR2_TEN;
}

// A simple method of sending a string to the serial port.
//   Set a pointer to the start of the string.
//   If the character pointed to is not a null character then Transfer the
//   character into the UART data register.
//   Wait until the data register has been sent (Transmission Empty is true).
//   Move the pointer on one byte.
//
//  Send a message to the debug port (UART2).
//
void UARTPrintf(char *message)
{
  char *ch = message;

  while (*ch) {
    // Put the next character into the data transmission register.
    UART2_DR = (unsigned char) *ch;
    // Wait for transmission to complete.
    while ((UART2_SR & UART2_SR_TXE) == 0);
    ch++;
  }
}


// Sample main program to control the application.
//
//  Main program loop.
//
/*
void main() // No interrupts
{
  char buffer[50];
  
  InitialiseSystemClock()
  InitializeUART()

  UARTPrintf("Hello from my microcontroller....\n\r");
  
  strcpy(temp, "Hello again ...\n\r");
  UARTPrintf(temp);

  int a = 10, b = 20;
  sprintf(buffer, "Value a is %a and b is %b\n\r", a, b);
  UARTPrintf(buffer);
}
*/
