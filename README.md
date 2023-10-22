# PublishQueuePosixRK

*Version of publish queue for storing events on the POSIX flash file system*

This library works a bit differently than [PublishQueueAsyncRK](https://github.com/rickkas7/PublishQueueAsyncRK):

- It only works with the POSIX flash file system
- It can keep a number of events in regular memory for efficiency and reduced flash wear
- Or it can always write events to the file system for maximum prevention of event loss
- The file system queue supports discarding the oldest events when the size limit is exceeded

## Usage

In many cases, you simply call this from setup:

```cpp
PublishQueuePosix::instance().setup();
```

And this from loop:

```cpp
PublishQueuePosix::instance().setup();
```

To publish you do something like this:

```
PublishQueuePosix::instance().publish("testEvent", buf, PRIVATE | WITH_ACK);
```

### RAM Queue

One parameter you may want to change is the RAM queue size:

```cpp
PublishQueuePosix::instance().withRamQueueSize(0);
```

Setting it to 0 means all events will be written to the file system immediately to reduce the chance of 
losing an event. This has higher overhead and can cause flash wear if you are publishing very frequently. 

The default is 2. However, if you normally burst out multiple events at a time, be sure to set the RAM 
queue size larger than the maximum number of events you burst out. If the RAM queue becomes full, all events 
will be written to the file system.

The RAM queue is also written to the file system if a publish fails, and right before a reset caused by 
a software update. However, on other resets the queue will be lost, so if you must not lose an event 
you should set the RAM queue size to 0.

### File Queue

The default maximum file queue size is 100, which corresponds to 100 events. Each event takes is stored in 
a single file. In many cases, an event will fit in a single 512-byte flash sector, but it could require two,
or three, for a full 1024 byte event with the overhead. 

Also remember that events can only be sent out one per second, so a very long queue will take a while to send!

```cpp
PublishQueuePosix::instance().withFileQueueSize(50);
```

## Dependencies

This library depends on two additional libraries:

- [SequentialFileRK](https://github.com/rickkas7/SequentialFileRK) manages the queue on the flash file system
- [BackgroundPublishRK](https://github.com/rickkas7/BackgroundPublishRK) handles publishing from a background thread


## API

---

### void PublishQueuePosix::setup() 

You must call this from setup() to initialize this library.

```
void setup()
```

---

### void PublishQueuePosix::loop() 

You must call the loop method from the global loop() function!

```
void loop()
```

---

### PublishQueuePosix & PublishQueuePosix::withRamQueueSize(size_t size) 

Sets the RAM based queue size (default is 2)

```
PublishQueuePosix & withRamQueueSize(size_t size)
```

#### Parameters
* `size` The size to set (can be 0, default is 2)

You can set this to 0 and the events will be stored on the flash file system immediately. This is the best option if the events must not be lost in the event of a sudden reboot.

It's more efficient to have a small RAM-based queue and it eliminates flash wear. Make sure you set the size larger than the maximum number of events you plan to send out in bursts, as if you exceed the RAM queue size, all outstanding events will be moved to files.

---

### size_t PublishQueuePosix::getRamQueueSize() const 

Gets the size of the RAM queue.

```
size_t getRamQueueSize() const
```

---

### PublishQueuePosix & PublishQueuePosix::withFileQueueSize(size_t size) 

Sets the file-based queue size (default is 100)

```
PublishQueuePosix & withFileQueueSize(size_t size)
```

#### Parameters
* `size` The maximum number of files to store (one event per file)

If you exceed this number of events, the oldest event is discarded.

---

### size_t PublishQueuePosix::getFileQueueSize() const 

Gets the file queue size.

```
size_t getFileQueueSize() const
```

---

### PublishQueuePosix & PublishQueuePosix::withDirPath(const char * dirPath) 

Sets the directory to use as the queue directory. This is required!

```
PublishQueuePosix & withDirPath(const char * dirPath)
```

#### Parameters
* `dirPath` the pathname, Unix-style with / as the directory separator.

Typically you create your queue either at the top level ("/myqueue") or in /usr ("/usr/myqueue"). The directory will be created if necessary, however only one level of directory will be created. The parent must already exist.

The dirPath can end with a slash or not, but if you include it, it will be removed.

You must call this as you cannot use the root directory as a queue!

---

### const char * PublishQueuePosix::getDirPath() const 

Gets the directory path set using withDirPath()

```
const char * getDirPath() const
```

The returned path will not end with a slash.

---

### bool PublishQueuePosix::publish(const char * eventName, PublishFlags flags1, PublishFlags flags2) 

Overload for publishing an event.

```
bool publish(const char * eventName, PublishFlags flags1, PublishFlags flags2)
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `flags1` Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.

* `flags2` (optional) You can use NO_ACK or WITH_ACK if desired.

#### Returns
true if the event was queued or false if it was not.

This function almost always returns true. If you queue more events than fit in the buffer the oldest (sometimes second oldest) is discarded.

---

### bool PublishQueuePosix::publish(const char * eventName, const char * data, PublishFlags flags1, PublishFlags flags2) 

Overload for publishing an event.

```
bool publish(const char * eventName, const char * data, PublishFlags flags1, PublishFlags flags2)
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `data` The event data (255 bytes maximum, 622 bytes in system firmware 0.8.0-rc.4 and later).

* `flags1` Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.

* `flags2` (optional) You can use NO_ACK or WITH_ACK if desired.

#### Returns
true if the event was queued or false if it was not.

This function almost always returns true. If you queue more events than fit in the buffer the oldest (sometimes second oldest) is discarded.

---

### bool PublishQueuePosix::publish(const char * eventName, const char * data, int ttl, PublishFlags flags1, PublishFlags flags2) 

Overload for publishing an event.

```
bool publish(const char * eventName, const char * data, int ttl, PublishFlags flags1, PublishFlags flags2)
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `data` The event data (255 bytes maximum, 622 bytes in system firmware 0.8.0-rc.4 and later).

* `ttl` The time-to-live value. If not specified in one of the other overloads, the value 60 is used. However, the ttl is ignored by the cloud, so it doesn't matter what you set it to. Essentially all events are discarded immediately if not subscribed to so they essentially have a ttl of 0.

* `flags1` Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.

* `flags2` (optional) You can use NO_ACK or WITH_ACK if desired.

#### Returns
true if the event was queued or false if it was not.

This function almost always returns true. If you queue more events than fit in the buffer the oldest (sometimes second oldest) is discarded.

---

### bool PublishQueuePosix::publishCommon(const char * eventName, const char * data, int ttl, PublishFlags flags1, PublishFlags flags2) 

Common publish function. All other overloads lead here. This is a pure virtual function, implemented in subclasses.

```
virtual bool publishCommon(const char * eventName, const char * data, int ttl, PublishFlags flags1, PublishFlags flags2)
```

#### Parameters
* `eventName` The name of the event (63 character maximum).

* `data` The event data (255 bytes maximum, 622 bytes in system firmware 0.8.0-rc.4 and later).

* `ttl` The time-to-live value. If not specified in one of the other overloads, the value 60 is used. However, the ttl is ignored by the cloud, so it doesn't matter what you set it to. Essentially all events are discarded immediately if not subscribed to so they essentially have a ttl of 0.

* `flags1` Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.

* `flags2` (optional) You can use NO_ACK or WITH_ACK if desired.

#### Returns
true if the event was queued or false if it was not.

This function almost always returns true. If you queue more events than fit in the buffer the oldest (sometimes second oldest) is discarded.

---

### void PublishQueuePosix::writeQueueToFiles() 

If there are events in the RAM queue, write them to files in the flash file system.

```
void writeQueueToFiles()
```

---

### void PublishQueuePosix::clearQueues() 

Empty both the RAM and file based queues. Any queued events are discarded.

```
void clearQueues()
```

---

### void PublishQueuePosix::setPausePublishing(bool value) 

Pause or resume publishing events.

```
void setPausePublishing(bool value)
```

#### Parameters
* `value` The value to set, true = pause, false = normal operation

If called while a publish is in progress, that publish will still proceed, but the next event (if any) will not be attempted.

This is used by the automated test tool; you probably won't need to manually manage this under normal circumstances.

---

### bool PublishQueuePosix::getPausePublishing() const 

Gets the state of the pause publishing flag.

```
bool getPausePublishing() const
```
---

### bool PublishQueuePosix::getCanSleep() const 

Determine if it's a good time to go to sleep. Added in version 0.0.3.

```
bool getCanSleep() const
```

If a publish is not in progress and the queue is empty, returns true.

If pausePublishing is true, then return true if either the current publish has completed, or not cloud connected.

---

### size_t PublishQueuePosix::getNumEvents() 

Gets the total number of events queued.

```
size_t getNumEvents()
```

This is the number of events in the RAM-based queue and the file-based queue. This operation is fast; the file queue length is stored in RAM, so this command does not need to access the file system.

If an event is currently being sent, the result includes this event.

---

### void PublishQueuePosix::lock() 

Lock the queue protection mutex.

```
void lock()
```

This is done internally; you probably won't need to call this yourself. It needs to be public for the WITH_LOCK() macro to work properly.

---

### bool PublishQueuePosix::tryLock() 

Attempt the queue protection mutex.

```
bool tryLock()
```

---

### void PublishQueuePosix::unlock() 

Unlock the queue protection mutex.

```
void unlock()
```

## Version History

### 0.0.6 (2023-10-22)

- Added `withPublishCompleteUserCallback()` to set a callback function or C++ lambda that is called when 
publish is complete (successfully or not).

### 0.0.5 (2022-10-06)

- I believe I fixed a situation where getCanSleep() can return true during the waitAfterConnect period after connecting
even though there are events in the queue.

### 0.0.4 (2022-06-21)

- When setPausePublishing(false), set the canSleep flag to false if there are events in the queue
- The canSleep flag was not set after sending the last event

### 0.0.3 (2022-03-07)

- Added getCanSleep() method to determine if the queue has been sent and it's safe to sleep. 

### 0.0.2 (2022-01-28)

- Rename BackgroundPublishRK class to BackgroundPublishRK to avoid conflict with a class of the same name in Tracker Edge.


### 0.0.1 (2021-04-28)

- Initial version
