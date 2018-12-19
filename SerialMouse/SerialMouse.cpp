/*
 * File: SerialMouse.cpp
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

#include "SerialMouse.hpp"

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(SerialMouseResources, IOService)
OSDefineMetaClassAndStructors(SerialMouse, IOHIPointing)

IOService *SerialMouse::probe(IOService *provider, SInt32 *score) {
    DBGLOG("SerialMouse::probe(): start\n");
    if (!super::probe(provider, score))
        return NULL;
    
    // Create variables.
    IORS232SerialStreamSync *serialStream = (IORS232SerialStreamSync*)provider;
    UInt32 origDataRate = 0, origDataSize = 0, origStopBits = 0, origFlowControl = 0;
    IOService *returnService = NULL;
    
    // Acquire port.
    if (acquirePort(serialStream) != kIOReturnSuccess)
        goto fail;
    
    // Save original port settings, setup stream, and check mouse ID
    if ((getPortSettings(serialStream, &origDataRate, &origDataSize, &origStopBits, &origFlowControl) != kIOReturnSuccess) ||
        (setupPort(serialStream) != kIOReturnSuccess) || (checkMouseId(serialStream) != kIOReturnSuccess))
        goto fail;
    
    // There is a serial mouse present, so we can match.
    returnService = this;
    goto done;
    
fail:
    DBGLOG("SerialMouse::probe(): no mouse found\n");
    returnService = NULL;
    
done:
    // Close port and restore port settings.
    setPortSettings(serialStream, origDataRate, origDataSize, origStopBits, origFlowControl);
    releasePort(serialStream);
    return returnService;
}

bool SerialMouse::start(IOService *provider) {
    DBGLOG("SerialMouse::start(): start\n");
    if (!super::start(provider))
        return false;
    
    // Acquire port, setup stream, and check mouse ID.
    serialStream = (IORS232SerialStreamSync*)provider;
    if ((acquirePort(serialStream) != kIOReturnSuccess) || (setupPort(serialStream) != kIOReturnSuccess) ||
        (checkMouseId(serialStream) != kIOReturnSuccess))
        goto fail;
    
    // Initialize polling thread.
    if (kernel_thread_start(OSMemberFunctionCast(thread_continue_t, this, &SerialMouse::pollMouseThread),
                            this, &pollThread) != kIOReturnSuccess)
        goto fail;
    
    // Driver started successfully.
    return true;
    
fail:
    DBGLOG("SerialMouse::start(): failed to start mouse\n");
    releasePort(serialStream);
    return false;
}

void SerialMouse::stop(IOService *provider) {
    DBGLOG("SerialMouse::stop(): start\n");
    
    // Kill polling thread.
    thread_terminate(pollThread);
    thread_deallocate(pollThread);
    
    // Stop superclass.
    super::stop(provider);
}

void SerialMouse::pollMouseThread(void) {
    DBGLOG("SerialMouse::pollMouseThread(): start\n");
    while (true) {
        UInt8 packet[MOUSE_PACKET_LENGTH];
        UInt32 count = 0;
        
        // Read packet.
        if ((serialStream->dequeueData(packet, MOUSE_PACKET_LENGTH, &count, MOUSE_PACKET_LENGTH) == kIOReturnSuccess) &&
            count == MOUSE_PACKET_LENGTH) {
            DBGLOG("SerialMouse::pollMouseThread(): got packet %X %X %X\n",
                   packet[0], packet[1], packet[2]);
            
            // If first byte is invalid, flush buffer.
            if (!MOUSE_PACKET_VALID(packet))
                flushPort(serialStream);
            else { // Packet is valid, send to HID system.
                // Get current time.
                uint64_t now_abs;
                clock_get_uptime(&now_abs);
                uint64_t now_ns;
                absolutetime_to_nanoseconds(now_abs, &now_ns);

                // Dispatch pointer movement event.
                dispatchRelativePointerEvent(MOUSE_PACKET_POSX(packet), MOUSE_PACKET_POSY(packet),
                                             MOUSE_PACKET_BUTTONS(packet), *(AbsoluteTime*)&now_ns);
            }
        }
    }
}

IOReturn SerialMouse::acquirePort(IORS232SerialStreamSync *serialStream) {
    DBGLOG("SerialMouse::acquirePort(): start\n");
    
    // Acquire port.
    return serialStream->acquirePort(false);
}

IOReturn SerialMouse::releasePort(IORS232SerialStreamSync *serialStream) {
    DBGLOG("SerialMouse::releasePort(): start\n");
    IOReturn status;
    
    // Deactivate port.
    status = serialStream->executeEvent(PD_E_ACTIVE, false);
    if (status != kIOReturnSuccess)
        return status;
    
    // Release port.
    return serialStream->releasePort();
}

IOReturn SerialMouse::setupPort(IORS232SerialStreamSync *serialStream) {
    DBGLOG("SerialMouse::setupPort(): start\n");
    IOReturn status;
    
    // Set up port.
    status = setPortSettings(serialStream, MOUSE_DATA_RATE, MOUSE_DATA_SIZE, MOUSE_STOP_BITS, MOUSE_FLOW_CONTROL);
    if (status != kIOReturnSuccess)
        return status;
    
    // Activate port.
    return serialStream->executeEvent(PD_E_ACTIVE, true);
}

IOReturn SerialMouse::flushPort(IORS232SerialStreamSync *serialStream) {
    DBGLOG("SerialMouse::flushPort(): start\n");
    return serialStream->executeEvent(PD_E_RXQ_FLUSH, 0);
}

IOReturn SerialMouse::checkMouseId(IORS232SerialStreamSync *serialStream) {
    DBGLOG("SerialMouse::checkMouseId(): start\n");
    IOReturn status;
    UInt8 mouseId = 0;
    UInt32 count = 0;
    
    // Flush receive buffer.
    status = flushPort(serialStream);
    if (status != kIOReturnSuccess)
        return status;
    
    // Toggle DTR bit.
    status = serialStream->executeEvent(PD_E_FLOW_CONTROL, MOUSE_FLOW_CONTROL);
    if (status != kIOReturnSuccess)
        return status;
    status = serialStream->executeEvent(PD_E_FLOW_CONTROL, MOUSE_FLOW_CONTROL & ~PD_RS232_S_DTR);
    if (status != kIOReturnSuccess)
        return status;
    status = serialStream->executeEvent(PD_E_FLOW_CONTROL, MOUSE_FLOW_CONTROL);
    if (status != kIOReturnSuccess)
        return status;
    
    // Read ID byte.
    IOSleep(MOUSE_ID_DELAY_MS);
    status = serialStream->dequeueData(&mouseId, 1, &count, 0);
    DBGLOG("SerialMouse::checkMouseId(): device returned ID byte 0x%X\n", mouseId);
    if (status != kIOReturnSuccess)
        return status;
    
    // Ensure mouse ID byte is valid.
    if (mouseId != MOUSE_ID_BYTE)
        return kIOReturnInvalid;
    
    // Success.
    return kIOReturnSuccess;
}

IOReturn SerialMouse::getPortSettings(IORS232SerialStreamSync *serialStream, UInt32 *dataRate,
                         UInt32 *dataSize, UInt32 *stopBits, UInt32 *flowControl) {
    DBGLOG("SerialMouse::getPortSettings(): start\n");
    IOReturn status;
    
    // Get port settings.
    status = serialStream->requestEvent(PD_E_DATA_RATE, dataRate);
    if (status != kIOReturnSuccess)
        return status;
    status = serialStream->requestEvent(PD_E_DATA_SIZE, dataSize);
    if (status != kIOReturnSuccess)
        return status;
    status = serialStream->requestEvent(PD_RS232_E_STOP_BITS, stopBits);
    if (status != kIOReturnSuccess)
        return status;
    return serialStream->requestEvent(PD_E_FLOW_CONTROL, flowControl);
}

IOReturn SerialMouse::setPortSettings(IORS232SerialStreamSync *serialStream, UInt32 dataRate,
                         UInt32 dataSize, UInt32 stopBits, UInt32 flowControl) {
    DBGLOG("SerialMouse::setPortSettings(): start\n");
    IOReturn status;
    
    // Get port settings.
    status = serialStream->executeEvent(PD_E_DATA_RATE, dataRate);
    if (status != kIOReturnSuccess)
        return status;
    status = serialStream->executeEvent(PD_E_DATA_SIZE, dataSize);
    if (status != kIOReturnSuccess)
        return status;
    status = serialStream->executeEvent(PD_RS232_E_STOP_BITS, stopBits);
    if (status != kIOReturnSuccess)
        return status;
    return serialStream->executeEvent(PD_E_FLOW_CONTROL, flowControl);
}
