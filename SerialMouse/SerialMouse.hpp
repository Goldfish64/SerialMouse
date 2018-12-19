/*
 * File: SerialMouse.hpp
 *
 * Copyright (c) 2018 John Davis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SerialMouse_hpp
#define SerialMouse_hpp

// Common I/O Kit includes.
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>

// HID and serial includes.
#include <IOKit/hidsystem/IOHIPointing.h>
#include <IOKit/serial/IORS232SerialStreamSync.h>

// Debug logging.
#if DEBUG
#define DBGLOG(args...) IOLog(args)
#else
#define DBGLOG(args...) ;
#endif

// Bits.
#define bits <<1

// Serial settings for MS protocol.
#define MOUSE_DATA_RATE     (1200 bits)
#define MOUSE_DATA_SIZE     (7 bits)
#define MOUSE_STOP_BITS     (1 bits)
#define MOUSE_FLOW_CONTROL  (PD_RS232_S_RTS | PD_RS232_S_DTR)
#define MOUSE_ID_DELAY_MS   100
#define MOUSE_ID_BYTE       0x4D

#define MOUSE_POLL_DELAY_MS 100

#define MOUSE_PACKET_LENGTH     3
#define MOUSE_PACKET_HEADER_BIT 0x40
#define MOUSE_PACKET_LEFTB_BIT  0x20
#define MOUSE_PACKET_RIGHTB_BIT 0x10

#define MOUSE_PACKET_VALID(packet)  ((Boolean)(packet[0] & MOUSE_PACKET_HEADER_BIT))
#define MOUSE_PACKET_LEFTB(packet)  ((Boolean)(packet[0] & MOUSE_PACKET_LEFTB_BIT))
#define MOUSE_PACKET_RIGHTB(packet) ((Boolean)(packet[0] & MOUSE_PACKET_RIGHTB_BIT))
#define MOUSE_PACKET_POSX(packet)   ((SInt8)((packet[1] & 0x3F) | ((packet[0] & 0x3) << 6)))
#define MOUSE_PACKET_POSY(packet)   ((SInt8)((packet[2] & 0x3F) | ((packet[0] & 0xC) << 4)))

// SerialMouseResources class. This is used to keep the kext in memory.
class SerialMouseResources : IOService {
    OSDeclareDefaultStructors(SerialMouseResources);
};

// SerialMouse class.
class SerialMouse : IOHIPointing {
    typedef IOHIPointing super;
    OSDeclareDefaultStructors(SerialMouse);
    
public:
    virtual IOService *probe(IOService *provider, SInt32 *score);
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    
private:
    // Serial stream.
    IORS232SerialStreamSync *serialStream;
    
    // Polling thread.
    thread_t pollThread;
    void pollMouseThread(void);
    
    static IOReturn setupPort(IORS232SerialStreamSync *serialStream);
    static IOReturn closePort(IORS232SerialStreamSync *serialStream);
    static IOReturn flushPort(IORS232SerialStreamSync *serialStream);
    static IOReturn checkMouseId(IORS232SerialStreamSync *serialStream);
    
    static IOReturn getPortSettings(IORS232SerialStreamSync *serialStream, UInt32 *dataRate,
                                    UInt32 *dataSize, UInt32 *stopBits, UInt32 *flowControl);
    static IOReturn setPortSettings(IORS232SerialStreamSync *serialStream, UInt32 dataRate,
                                    UInt32 dataSize, UInt32 stopBits, UInt32 flowControl);
};

#endif
