# Automated Test - PublishQueuePosixRK

This is the automated test tool for the library. It's kind of difficult to use, but it has a 
number of useful features you might want to adapt for your own projects:

- The Particle device runs special firmware that can run tests, controlled by USB serial
- The node.js test tool monitors the USB serial output of the Particle device to look for logging messages
- The test tools sends commands over USB serial to control the on-device behavior
- It connects to the Particle cloud server-sent-events stream for the device to monitor published events
- It also acts as a device service, monitoring cloud data transmission and causing data loss as needed for some tests



### Config File

#### dsAddr

```
dig 1.udp.particle.io
```

