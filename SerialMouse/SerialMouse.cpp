//
//  SerialMouse.cpp
//  Serial mouse driver for macOS.
//
//  Copyright © 2018-2023 Goldfish64. All rights reserved.
//

#include "SerialMouse.hpp"

OSDefineMetaClassAndStructors(SerialMouseResources, IOService)
OSDefineMetaClassAndStructors(SerialMouse, IOHIPointing)

IOService *SerialMouse::probe(IOService *provider, SInt32 *score) {
  DBGLOG("SerialMouse: probe()\n");
  if (!super::probe(provider, score)) {
    return nullptr;
  }

  IOReturn status;
  bool probed = false;
  IOSerialStreamSync *serialStream = OSDynamicCast(IOSerialStreamSync, provider);
  UInt32 origDataRate = 0, origDataSize = 0, origStopBits = 0, origFlowControl = 0;
  
  if (serialStream == nullptr) {
    SYSLOG("SerialMouse: Provider is not a serial stream\n");
    return nullptr;
  }

  //
  // Acquire serial port.
  //
  if (acquirePort(serialStream) != kIOReturnSuccess) {
    return nullptr;
  }

  //
  // Save original port settings, setup stream, and check mouse ID.
  //
  do {
    status = getPortSettings(&origDataRate, &origDataSize, &origStopBits, &origFlowControl);
    if (status != kIOReturnSuccess) {
      SYSLOG("SerialMouse: Failed to set serial port settings during probe()\n");
      break;
    }

    status = setupPort();
    if (status != kIOReturnSuccess) {
      SYSLOG("SerialMouse: Failed to setup serial port during probe()\n");
      break;
    }

    status = checkMouseId();
    if (status != kIOReturnSuccess) {
      SYSLOG("SerialMouse: Device on serial port is not a serial mouse\n");
      break;
    }

    probed = true;
  } while (false);

  //
  // Restore port settings and release serial port.
  //
  setPortSettings(origDataRate, origDataSize, origStopBits, origFlowControl);
  releasePort();
  return probed ? this : nullptr;
}

bool SerialMouse::start(IOService *provider) {
  DBGLOG("SerialMouse: Starting\n");

  IOReturn status;
  bool started = false;
  IOSerialStreamSync *serialStream = OSDynamicCast(IOSerialStreamSync, provider);

  if (serialStream == nullptr) {
    SYSLOG("SerialMouse: Provider is not a serial stream\n");
    return false;
  }

  if (!super::start(provider)) {
    return false;
  }

  //
  // Setup port and start polling.
  //
  do {
    status = acquirePort(serialStream);
    if (status != kIOReturnSuccess) {
      SYSLOG("SerialMouse: Failed to acquire serial port\n");
      break;
    }

    status = setupPort();
    if (status != kIOReturnSuccess) {
      SYSLOG("SerialMouse: Failed to setup serial port during probe()\n");
      break;
    }

    status = kernel_thread_start(OSMemberFunctionCast(thread_continue_t, this, &SerialMouse::pollMouseThread),
                                 this, &_pollThread);
    if (status != kIOReturnSuccess) {
      SYSLOG("SerialMouse: Polling thread could not be created\n");
      break;
    }

    started = true;
    SYSLOG("SerialMouse: Serial mouse started\n");
  } while (false);

  if (!started) {
    stop(provider);
  }
  return started;
}

void SerialMouse::stop(IOService *provider) {
  //
  // Kill polling thread.
  //
  thread_terminate(_pollThread);
  thread_deallocate(_pollThread);
  releasePort();

  super::stop(provider);
}

void SerialMouse::pollMouseThread(void) {
  DBGLOG("SerialMouse: Polling thread\n");
  while (true) {
    UInt8 packet[MOUSE_PACKET_LENGTH];
    UInt32 count = 0;

    //
    // Read incoming packet.
    //
    if ((_serialStream->dequeueData(packet, MOUSE_PACKET_LENGTH, &count, MOUSE_PACKET_LENGTH) == kIOReturnSuccess)
      && count == MOUSE_PACKET_LENGTH) {
      DBGLOG("SerialMouse::pollMouseThread(): got packet %X %X %X\n", packet[0], packet[1], packet[2]);

      // If first byte is invalid, flush buffer.
      if (!MOUSE_PACKET_VALID(packet)) {
        flushPort();
      } else { // Packet is valid, send to HID system.
        // Get current time.
        uint64_t now_abs;
        clock_get_uptime(&now_abs);
        uint64_t now_ns;
        absolutetime_to_nanoseconds(now_abs, &now_ns);

        //
        // Dispatch pointer movement event.
        //
        dispatchRelativePointerEvent(MOUSE_PACKET_POSX(packet), MOUSE_PACKET_POSY(packet),
                                     MOUSE_PACKET_BUTTONS(packet), *(AbsoluteTime*)&now_ns);
      }
    }
  }
}

IOReturn SerialMouse::acquirePort(IOSerialStreamSync *serialStream) {
  DBGLOG("SerialMouse: Acquiring serial port\n");

  IOReturn status = serialStream->acquirePort(false);
  if (status != kIOReturnSuccess) {
    SYSLOG("SerialMouse: Failed to acquire serial port with status 0x%X\n", status);
    return status;
  }

  _serialStream = serialStream;
  _serialStream->retain();
  return status;
}

void SerialMouse::releasePort() {
  DBGLOG("SerialMouse: Releasing serial port\n");

  //
  // Deactivate and release port.
  //
  if (_serialStream != nullptr) {
    if (_serialStream->executeEvent(PD_E_ACTIVE, false) == kIOReturnSuccess) {
      _serialStream->releasePort();
    }
  }
  OSSafeReleaseNULL(_serialStream);
}

IOReturn SerialMouse::setupPort() {
  DBGLOG("SerialMouse: Setting up port\n");
  IOReturn status;

  //
  // Set up and activate port.
  //
  status = setPortSettings(MOUSE_DATA_RATE, MOUSE_DATA_SIZE, MOUSE_STOP_BITS, MOUSE_FLOW_CONTROL);
  if (status != kIOReturnSuccess) {
    return status;
  }
  return _serialStream->executeEvent(PD_E_ACTIVE, true);
}

IOReturn SerialMouse::flushPort() {
  DBGLOG("SerialMouse: Flushing port\n");
  return _serialStream->executeEvent(PD_E_RXQ_FLUSH, 0);
}

IOReturn SerialMouse::checkMouseId() {
  DBGLOG("SerialMouse: Checking mouse ID\n");
  IOReturn status;
  UInt8 mouseId = 0;
  UInt32 count = 0;

  //
  // Flush receive buffer.
  //
  status = flushPort();
  if (status != kIOReturnSuccess) {
    return status;
  }

  //
  // Toggle DTR bit.
  //
  status = _serialStream->executeEvent(PD_E_FLOW_CONTROL, MOUSE_FLOW_CONTROL);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = _serialStream->executeEvent(PD_E_FLOW_CONTROL, MOUSE_FLOW_CONTROL & ~PD_RS232_S_DTR);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = _serialStream->executeEvent(PD_E_FLOW_CONTROL, MOUSE_FLOW_CONTROL);
  if (status != kIOReturnSuccess) {
    return status;
  }

  //
  // Read ID byte.
  //
  IOSleep(MOUSE_ID_DELAY_MS);
  status = _serialStream->dequeueData(&mouseId, 1, &count, 0);
  DBGLOG("SerialMouse::checkMouseId(): device returned ID byte 0x%X\n", mouseId);
  if (status != kIOReturnSuccess) {
    return status;
  }

  //
  // Ensure mouse ID byte is valid.
  //
  if (mouseId != MOUSE_ID_BYTE) {
    return kIOReturnInvalid;
  }
  return kIOReturnSuccess;
}

IOReturn SerialMouse::getPortSettings(UInt32 *dataRate, UInt32 *dataSize, UInt32 *stopBits, UInt32 *flowControl) {
  DBGLOG("SerialMouse: Get port settings\n");
  IOReturn status;

  //
  // Get port settings.
  //
  status = _serialStream->requestEvent(PD_E_DATA_RATE, dataRate);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = _serialStream->requestEvent(PD_E_DATA_SIZE, dataSize);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = _serialStream->requestEvent(PD_RS232_E_STOP_BITS, stopBits);
  if (status != kIOReturnSuccess) {
    return status;
  }
  return _serialStream->requestEvent(PD_E_FLOW_CONTROL, flowControl);
}

IOReturn SerialMouse::setPortSettings(UInt32 dataRate, UInt32 dataSize, UInt32 stopBits, UInt32 flowControl) {
  DBGLOG("SerialMouse: Set port settings(%u,%u,%u,%u)\n", dataRate, dataSize, stopBits, flowControl);
  IOReturn status;

  //
  // Set port settings.
  //
  status = _serialStream->executeEvent(PD_E_DATA_RATE, dataRate);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = _serialStream->executeEvent(PD_E_DATA_SIZE, dataSize);
  if (status != kIOReturnSuccess) {
    return status;
  }
  status = _serialStream->executeEvent(PD_RS232_E_STOP_BITS, stopBits);
  if (status != kIOReturnSuccess) {
    return status;
  }
  return _serialStream->executeEvent(PD_E_FLOW_CONTROL, flowControl);
}
