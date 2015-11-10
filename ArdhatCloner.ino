/// ArdhatCloner
//------------------
//
/// Upload a bootloader and the Ardhat Factory Sketch to a second board.
//  The uploaded file is in data.h and can be created with tcl.hex by JC Wippler.
//
// (Note that Ardhat uses a modified Optiboot which blinks D9 during upload)
//
// This code is adapted from isp_prepare. It leaves the serial port in tristate
// so that Ardhat being programmed can then use terminal program to run self-test.
// 2015-10-06 <hello@ubiqio.com> http://opensource.org/licenses/mit-license.php
//
// This code is adapted from isp_prepare. It omits the button and run LED,
// and starts right away. 
//
// Progress is output to software serial on pins (14, 15); // RX, TX on A0, A1
// at 4800bps, and Ardhat will pulse status LED when it has been succesfully programmed
//
// The 6 ISP pins of the target board need to be connected to the board running
// this sketch as follows (using Arduino pin naming):
//
//   ISP pin 1  <->  digital 4  (MISO)          ISP CONNECTOR
//   ISP pin 2  <->  VCC                          +---+---+
//   ISP pin 3  <->  digital 6  (SCK)             | 1 | 2 |
//   ISP pin 4  <->  digital 5  (MOSI)            | 3 | 4 |
//   ISP pin 5  <->  digital 7  (RESET)           | 5 | 6 |
//   ISP pin 6  <->  ground                       +---+---+
//
//
//
/// Adapted from jc wipplers isp_repair
// 2020-05-29 <jc@wippler.nl> http://opensource.org/licenses/mit-license.php
// see http://jeelabs.org/2010/04/25/preparing-atmegas-with-isp/
//     http://jeelabs.org/2011/05/17/switching-to-optiboot/
//     http://jeelabs.org/2011/05/26/fixing-the-isp_repair-sketch/
//     http://jeelabs.org/2011/05/29/summary-of-isp-options/
//
// Boot Cloner
// adapted from http://www.arduino.cc/playground/BootCloner/BootCloner
// original copyright notice: 2007 by Amplificar <mailto:amplificar@gmail.com>

#include <avr/pgmspace.h>
#include <avr/sleep.h>


#include <SoftwareSerial.h>

SoftwareSerial mySerial(14, 15); // RX, TX on A0, A1


#define OPTIBOOT    1   // 1 = OptiBoot, 0 = Duemilanove boot loader

// pin definitions
#define MISO        4   // to target MISO (ICSP pin 1)
#define MOSI        5   // to target MOSI (ICSP pin 4)
#define SCK         6   // to target SPI clock (ICSP pin 3)
#define RESET       7   // to target reset pin (ICSP pin 5)
//#define DONE_LED    9   // orange LED on Ardhat, blinks on start and when ok
#define DONE_LED    13  // orange LED on standard Arduino, blinks on start and when ok



// MPU-specific values
#define PAGE_BYTES      128  // ATmega168 and ATmega328
#define LOCK_BITS       0xCF
#define FUSE_LOW        0xFF
#if OPTIBOOT
#define FUSE_HIGH       0xDE // 512b for optiboot
#else
#define FUSE_HIGH       0xDA // 2048b for 2009 boot
#endif
#define FUSE_EXTENDED   0x06

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   Include data from a file generated in isp_prepare/ dir with this cmd:
        ./hex2c.tcl Blink.cpp.hex optiboot_atmega328.hex \
                    ATmegaBOOT_168_atmega328.hex >../isp_repair/data.h
*/

#include "data.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// ISP Command Words
#define CMD_Program_Enable      0xAC53
#define CMD_Erase_Flash         0xAC80
#define CMD_Poll                0xF000
#define CMD_Read_Flash_Low      0x2000
#define CMD_Read_Flash_High     0x2800
#define CMD_Load_Page_Low       0x4000
#define CMD_Load_Page_High      0x4800
#define CMD_Write_Page          0x4C00
#define CMD_Read_EEPROM         0xA000
#define CMD_Write_EEPROM        0xC000
#define CMD_Read_Lock           0x5800
#define CMD_Write_Lock          0xACE0
#define CMD_Read_Signature      0x3000
#define CMD_Write_Fuse_Low      0xACA0
#define CMD_Write_Fuse_High     0xACA8
#define CMD_Write_Fuse_Extended 0xACA4
#define CMD_Read_Fuse_Low       0x5000
#define CMD_Read_Fuse_High      0x5808
#define CMD_Read_Fuse_Extended  0x5008
#define CMD_Read_Fuse_High      0x5808
#define CMD_Read_Calibration    0x3800

static byte XferByte(byte v) {
    byte result = 0;
    for (byte i = 0; i < 8; ++i) {
        digitalWrite(MOSI, v & 0x80);
        digitalWrite(SCK, 0); // slow pulse, max 60KHz
        digitalWrite(SCK, 1);
        v <<= 1;
        result = (result << 1) | digitalRead(MISO);
    }
    return result;
}

// send 4 bytes to target microcontroller, returns the fourth MISO byte
static byte Send_ISP (word v01, byte v2 =0, byte v3 =0) {
    XferByte(v01 >> 8);
    XferByte(v01);
    XferByte(v2);
    return XferByte(v3);
}

static void Send_ISP_wait (word v01, byte v2 =0, byte v3 =0) {
    Send_ISP(v01, v2, v3);
    while (Send_ISP(CMD_Poll) & 1)
        ;
}

static void Reset_Target() {
    digitalWrite(RESET, 1);
    digitalWrite(SCK, 0); // has to be set LOW at startup, or PE fails
    delay(30);
    digitalWrite(RESET, 0);
    delay(30); // minimum delay here is 20ms for the ATmega8
}

// prints the 16 signature bytes (device codes)
static void Read_Signature() {
    mySerial.print("Signatures:");
    for (byte x = 0 ; x < 8 ; ++x) {
        mySerial.print(" ");
        mySerial.print(Send_ISP(CMD_Read_Signature, x), HEX);
    }
    mySerial.println("");
}

// prints the lock and fuse bits (no leading zeros)
static void Read_Fuse() {
    mySerial.print("Lock Bits: ");
    mySerial.println(Send_ISP(CMD_Read_Lock), HEX);
    mySerial.print("Fuses: low ");
    mySerial.print(Send_ISP(CMD_Read_Fuse_Low), HEX);
    mySerial.print(", high ");
    mySerial.print(Send_ISP(CMD_Read_Fuse_High), HEX);
    mySerial.print(", extended ");
    mySerial.println(Send_ISP(CMD_Read_Fuse_Extended), HEX);
    if (Send_ISP(CMD_Read_Lock) == LOCK_BITS &&
            Send_ISP(CMD_Read_Fuse_Low) == FUSE_LOW &&
                Send_ISP(CMD_Read_Fuse_High) == FUSE_HIGH &&
                    Send_ISP(CMD_Read_Fuse_Extended) == FUSE_EXTENDED)
        mySerial.println("Fuse bits OK.");
}

static word addr2page (word addr) {
    return (word)(addr & ~ (PAGE_BYTES-1)) >> 1;
}

static void LoadPage(word addr, const byte* ptr) {
    word cmd = addr & 1 ? CMD_Load_Page_High : CMD_Load_Page_Low;
    Send_ISP(cmd | (addr >> 9), addr >> 1, pgm_read_byte(ptr));
}

static void WritePage (word page) {
    Send_ISP_wait(CMD_Write_Page | (page >> 8), page);
}

static void WriteData (word start, const byte* data, word count) {
    word page = addr2page(start);
    for (word i = 0; i < count; i += 2) {
        if (page != addr2page(start)) {
            WritePage(page);
            mySerial.print('.');
            page = addr2page(start);
        }
        LoadPage(start++, data + i);
        LoadPage(start++, data + i + 1);
    }
    WritePage(page);
    mySerial.println();
}

static byte EnableProgramming () {
    Reset_Target();
    if (Send_ISP(CMD_Program_Enable, 0x22, 0x22) != 0x22) {
        mySerial.println("Program Enable FAILED");
        return 0;
    }
    return 1;
}

static void blink () {
  pinMode(DONE_LED, OUTPUT);
  digitalWrite(DONE_LED, 1); 
  delay(200); // blink briefly
  pinMode(DONE_LED, INPUT);
}

static byte programSection (byte index) {
    mySerial.println(sections[index].title);
    byte f = EnableProgramming();
    if (f)
        WriteData(sections[index].start, progdata + sections[index].off,
                      sections[index].count);
    return f;
}

void setup () {
    mySerial.begin(4800);
    mySerial.println("\n[ArdhatCloner]");
    blink();

    digitalWrite(SCK, 1);
    digitalWrite(MOSI, 1);
    digitalWrite(RESET, 1);

    
  
    pinMode(SCK, OUTPUT);
    pinMode(MOSI, OUTPUT);
    pinMode(RESET, OUTPUT);

  
    mySerial.println("\nStarting...");
    
    if (EnableProgramming()) {
        mySerial.println("Erasing Flash");
        Send_ISP_wait(CMD_Erase_Flash, 0x22, 0x22);

        if (EnableProgramming()) {
            mySerial.println("Setting Fuses");
            Send_ISP_wait(CMD_Write_Fuse_Low, 0, FUSE_LOW);
            Send_ISP_wait(CMD_Write_Fuse_High, 0, FUSE_HIGH);
            Send_ISP_wait(CMD_Write_Fuse_Extended, 0, FUSE_EXTENDED);
            Send_ISP_wait(CMD_Write_Lock, 0, LOCK_BITS);
    
            byte ok = programSection(0);
            if (ok)
                ok = programSection(OPTIBOOT ? 1 : 2);

            if (ok) {
                Read_Fuse();
                Read_Signature();
                mySerial.println("Done.");
                blink();    
            }
        }
    }
// tristate the O/Ps
    pinMode(SCK, INPUT);
    pinMode(MOSI, INPUT);
    pinMode(RESET, INPUT);
       
    digitalWrite(SCK, 0);
    digitalWrite(MOSI, 0);
    digitalWrite(RESET, 0);

    
    delay(10); // let the serial port finish
    mySerial.end(); //tristate the hw serial ports
    
    cli(); // stop responding to interrupts
    ADCSRA &= ~ bit(ADEN); // disable the ADC
    PRR = 0xFF; // disable all subsystems
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_mode();
    // total power down, can only wake up with a hardware reset
}

void loop () {}
