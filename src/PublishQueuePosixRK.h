#ifndef __PUBLISHQUEUEPOSIXRK_H
#define __PUBLISHQUEUEPOSIXRK_H

// Github: https://github.com/rickkas7/PublishQueuePosixRK
// License: MIT

#include "Particle.h"
#include "SequentialFileRK.h"
#include "Lockable.h"

#include <deque>

/**
 * @brief Structure stored before the event data in files on the flash file system
 * 
 * Each file is sequentially numbered and has one event. The contents of the file
 * are this header (8 bytes) followed by the PublishQueueEvent structure, which
 * is variably sized based on the size of the event.    
 */
struct PublishQueueFileHeader {
    uint32_t magic;         //!< PublishQueuePosix::FILE_MAGIC = 0x31b67663
    uint8_t version;        //!< PublishQueuePosix::FILE_VERSION = 1
    uint8_t headerSize;     //!< sizeof(PublishQueueFileHeader) = 8
    uint16_t nameLen;       //!< sizeof(PublishQueueEvent::eventName) = 64
};

/**
 * @brief Structure to hold an event in RAM or in files
 * 
 * In RAM, this structure is stored in the ramQueue. 
 * 
 * On the flash file system, each file contains one event and consists of the
 * PublishQueueFileHeader above (8 bytes) plus this structure.
 * 
 * Note that the eventData is specified as 1 byte here, but it's actually
 * sized to fit the event data with a null terminator.
 */
struct PublishQueueEvent {
    PublishFlags flags; //!< NO_ACK or WITH_ACK. Can use PRIVATE, but that's no longer needed.
    char eventName[particle::protocol::MAX_EVENT_NAME_LENGTH + 1]; //!< c-string event name (required)
    char eventData[1]; //!< Variable size event data
};

/**
 * @brief Class for asynchronous publishing of events
 * 
 */
class PublishQueuePosix {
public:
    /**
     * @brief Gets the singleton instance of this class
     * 
     * You cannot construct a PublishQueuePosix object as a global variable,
     * stack variable, or with new. You can only request the singleton instance.
     */
    static PublishQueuePosix &instance();

    /**
     * @brief Sets the RAM based queue size (default is 2)
     * 
     * @param size The size to set (can be 0, default is 2)
     * 
     * You can set this to 0 and the events will be stored on the flash
     * file system immediately. This is the best option if the events must
     * not be lost in the event of a sudden reboot. 
     * 
     * It's more efficient to have a small RAM-based queue and it eliminates
     * flash wear. Make sure you set the size larger than the maximum number
     * of events you plan to send out in bursts, as if you exceed the RAM
     * queue size, all outstanding events will be moved to files.
     */
    PublishQueuePosix &withRamQueueSize(size_t size);

    /**
     * @brief Gets the size of the RAM queue
     */
    size_t getRamQueueSize() const { return ramQueueSize; };

    /**
     * @brief Sets the file-based queue size (default is 100)
     * 
     * @param size The maximum number of files to store (one event per file)
     * 
     * If you exceed this number of events, the oldest event is discarded.
     */
    PublishQueuePosix &withFileQueueSize(size_t size);

    /**
     * @brief Gets the file queue size
     */
    size_t getFileQueueSize() const { return fileQueueSize; };

    /**
     * @brief Sets the directory to use as the queue directory. This is required!
     * 
     * @param dirPath the pathname, Unix-style with / as the directory separator. 
     * 
     * Typically you create your queue either at the top level ("/myqueue") or in /usr
     * ("/usr/myqueue"). The directory will be created if necessary, however only one
     * level of directory will be created. The parent must already exist.
     * 
     * The dirPath can end with a slash or not, but if you include it, it will be
     * removed.
     * 
     * You must call this as you cannot use the root directory as a queue!
     */
    PublishQueuePosix &withDirPath(const char *dirPath) { fileQueue.withDirPath(dirPath); return *this; };

    /**
     * @brief Gets the directory path set using withDirPath()
     * 
     * The returned path will not end with a slash.
     */
    const char *getDirPath() const { return fileQueue.getDirPath(); };

    /**
     * @brief You must call this from setup() to initialize this library
     */
    void setup(TimedLock *ParticlePublishLock);

    /**
     * @brief You must call the loop method from the global loop() function!
     */
    void loop();

	/**
	 * @brief Overload for publishing an event
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param flags1 Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.
	 *
	 * @param flags2 (optional) You can use NO_ACK or WITH_ACK if desired.
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
	 */
	inline bool publish(const char *eventName, PublishFlags flags1, PublishFlags flags2 = PublishFlags()) {
		return publishCommon(eventName, "", 60, flags1, flags2);
	}

	/**
	 * @brief Overload for publishing an event
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param data The event data (255 bytes maximum, 622 bytes in system firmware 0.8.0-rc.4 and later).
	 *
	 * @param flags1 Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.
	 *
	 * @param flags2 (optional) You can use NO_ACK or WITH_ACK if desired.
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
	 */
	inline bool publish(const char *eventName, const char *data, PublishFlags flags1, PublishFlags flags2 = PublishFlags()) {
		return publishCommon(eventName, data, 60, flags1, flags2);
	}

	/**
	 * @brief Overload for publishing an event
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param data The event data (255 bytes maximum, 622 bytes in system firmware 0.8.0-rc.4 and later).
	 *
	 * @param ttl The time-to-live value. If not specified in one of the other overloads, the value 60 is
	 * used. However, the ttl is ignored by the cloud, so it doesn't matter what you set it to. Essentially
	 * all events are discarded immediately if not subscribed to so they essentially have a ttl of 0.
	 *
	 * @param flags1 Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.
	 *
	 * @param flags2 (optional) You can use NO_ACK or WITH_ACK if desired.
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
	 */
	inline bool publish(const char *eventName, const char *data, int ttl, PublishFlags flags1, PublishFlags flags2 = PublishFlags()) {
		return publishCommon(eventName, data, ttl, flags1, flags2);
	}

	/**
	 * @brief Common publish function. All other overloads lead here. This is a pure virtual function, implemented in subclasses.
	 *
	 * @param eventName The name of the event (63 character maximum).
	 *
	 * @param data The event data (255 bytes maximum, 622 bytes in system firmware 0.8.0-rc.4 and later).
	 *
	 * @param ttl The time-to-live value. If not specified in one of the other overloads, the value 60 is
	 * used. However, the ttl is ignored by the cloud, so it doesn't matter what you set it to. Essentially
	 * all events are discarded immediately if not subscribed to so they essentially have a ttl of 0.
	 *
	 * @param flags1 Normally PRIVATE. You can also use PUBLIC, but one or the other must be specified.
	 *
	 * @param flags2 (optional) You can use NO_ACK or WITH_ACK if desired.
	 *
	 * @return true if the event was queued or false if it was not.
	 *
	 * This function almost always returns true. If you queue more events than fit in the buffer the
	 * oldest (sometimes second oldest) is discarded.
	 */
	virtual bool publishCommon(const char *eventName, const char *data, int ttl, PublishFlags flags1, PublishFlags flags2 = PublishFlags());

    /**
     * @brief If there are events in the RAM queue, write them to files in the flash file system
     */
    void writeQueueToFiles();

    /**
     * @brief Empty both the RAM and file based queues. Any queued events are discarded. 
     */
    void clearQueues();

    /**
     * @brief Pause or resume publishing events
     * 
     * @param value The value to set, true = pause, false = normal operation
     * 
     * If called while a publish is in progress, that publish will still proceed, but
     * the next event (if any) will not be attempted.
     * 
     * This is used by the automated test tool; you probably won't need to manually
     * manage this under normal circumstances.
     */
    void setPausePublishing(bool value);

    /**
     * @brief Gets the state of the pause publishing flag
     */
    bool getPausePublishing() const { return pausePublishing; };

    /**
     * @brief Determine if it's a good time to go to sleep
     * 
     * If a publish is not in progress and the queue is empty, returns true. 
     * 
     * If pausePublishing is true, then return true if either the current publish has
     * completed, or not cloud connected.
     */
    bool getCanSleep() const { return canSleep; };

    /**
     * @brief Gets the total number of events queued
     * 
     * This is the number of events in the RAM-based queue and the file-based
     * queue. This operation is fast; the file queue length is stored in RAM,
     * so this command does not need to access the file system.
     * 
     * If an event is currently being sent, the result includes this event.
     */
    size_t getNumEvents();

    /**
     * @brief Check the queue limit, discarding events as necessary
     * 
     * When the RAM queue exceeds the limit, all events are moved into files. 
     */
    void checkQueueLimits();
    
    /**
     * @brief Lock the queue protection mutex
     * 
     * This is done internally; you probably won't need to call this yourself.
     * It needs to be public for the WITH_LOCK() macro to work properly.
     */
    void lock() { os_mutex_recursive_lock(mutex); };

    /**
     * @brief Attempt the queue protection mutex
     */
    bool tryLock() { return os_mutex_recursive_trylock(mutex); };

    /**
     * @brief Unlock the queue protection mutex
     */
    void unlock() { os_mutex_recursive_unlock(mutex); };

    /**
     * @brief Magic bytes store at the beginning of event files for validity checking
     */
    static const uint32_t FILE_MAGIC = 0x31b67663;
    
    /**
     * @brief Version of the file header for events
     */
    static const uint8_t FILE_VERSION = 1;

protected:
    /**
     * @brief Constructor 
     * 
     * This class is a singleton; you never create one of these directly. Use 
     * PublishQueuePosix::instance() to get the singleton instance.
     */
    PublishQueuePosix();

    /**
     * @brief Destructor
     * 
     * This class is never deleted; once the singleton is created it cannot
     * be destroyed.
     */
    virtual ~PublishQueuePosix();

    /**
     * @brief This class is not copyable
     */
    PublishQueuePosix(const PublishQueuePosix&) = delete;

    /**
     * @brief This class is not copyable
     */
    PublishQueuePosix& operator=(const PublishQueuePosix&) = delete;

    /**
     * @brief Allocate a new event structure in RAM
     * 
     * The PublishEventQueue structure contains a header and is variably sized for the eventData.
     * 
     * May return NULL if eventName or eventData are invalid (too long) or out of memory.
     * 
     * You must delete the result from this method when you are done using it. 
     */
    PublishQueueEvent *newRamEvent(const char *eventName, const char *eventData, PublishFlags flags);

    /**
     * @brief Read an event from a sequentially numbered file 
     * 
     * @param fileNum The file number to read 
     * 
     * May return NULL if file does not exist, or out of memory.
     * 
     * You must delete the result from this method when you are done using it. 
     */
    PublishQueueEvent *readQueueFile(int fileNum);

    /**
     * @brief Callback for BackgroundPublishRK library
     */
    void publishCompleteCallback(bool succeeded, const char *eventName, const char *eventData);

    /**
     * @brief State handler for waiting to connect to the Particle cloud
     * 
     * Next state: stateWait
     */
    void stateConnectWait();

    /**
     * @brief State handler for waiting to publish
     * 
     * stateTime and durationMs determine whether to stay in this state waiting, or whether
     * to publish and go into statePublishWait.
     * 
     * Next state: statePublishWait or stateConnectWait
     */
    void stateWait();

    /**
     * @brief State handler for waiting for publish to complete
     * 
     * Next state: stateWait
     */
    void statePublishWait();

    /**
     * @brief SequentialFileRK library object for maintaining the queue of files on the POSIX file system
     */
    SequentialFile fileQueue;


    size_t ramQueueSize = 2; //!< size of the queue in RAM
    size_t fileQueueSize = 100; //!< size of the queue on the flash file system

    os_mutex_recursive_t mutex; //!< mutex for protecting the queue
    std::deque<PublishQueueEvent*> ramQueue; //!< Queue in RAM

    PublishQueueEvent *curEvent = 0; //!< Current event being published
    int curFileNum = 0; //!< Current file number being published (0 if from RAM queue)
    unsigned long stateTime = 0; //!< millis() value when entering the state, used for stateWait
    unsigned long durationMs = 0; //!< how long to wait before publishing in milliseconds, used in stateWait
    bool publishComplete = false; //!< true if the publish has completed (successfully or not)
    bool publishSuccess = false; //!< true if the publish succeeded
    bool pausePublishing = false; //!< flag to pause publishing (used from automated test)
    bool canSleep = false; //!< returns true if this is a good time to go to sleep

    unsigned long waitAfterConnect = 2000; //!< time to wait after Particle.connected() before publishing
    unsigned long waitBetweenPublish = 1000; //!< how long to wait in milliseconds between publishes
    unsigned long waitAfterFailure = 30000; //!< how long to wait after failing to publish before trying again

    std::function<void(PublishQueuePosix&)> stateHandler = 0; //!< state handler (stateConnectWait, stateWait, etc).

    static void systemEventHandler(system_event_t event, int param); //!< system event handler, used to detect reset events

    static PublishQueuePosix *_instance; //!< singleton instance of this class
};

#endif /* __PUBLISHQUEUEPOSIXRK_H */
