#ifndef __PUBLISHQUEUEPOSIXRK_H
#define __PUBLISHQUEUEPOSIXRK_H

#include "Particle.h"
#include "SequentialFileRK.h"

#include <deque>

struct PublishQueueFileHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t headerSize;
    uint16_t nameLen;
};

struct PublishQueueEvent {
    PublishFlags flags;
    char eventName[particle::protocol::MAX_EVENT_NAME_LENGTH + 1];
    char eventData[1]; // Variable size
};

class PublishQueuePosix {
public:
    static PublishQueuePosix &instance();

    /**
     * @brief You must call this from setup() to initialize this library
     */
    void setup();

    PublishQueuePosix &withRamQueueSize(size_t size) { ramQueueSize = size; return *this; };

    size_t getRamQueueSize() const { return ramQueueSize; };

    PublishQueuePosix &withFileQueueSize(size_t size) { fileQueueSize = size; return *this; };

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
	inline 	bool publish(const char *eventName, PublishFlags flags1, PublishFlags flags2 = PublishFlags()) {
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
	inline  bool publish(const char *eventName, const char *data, PublishFlags flags1, PublishFlags flags2 = PublishFlags()) {
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
	inline  bool publish(const char *eventName, const char *data, int ttl, PublishFlags flags1, PublishFlags flags2 = PublishFlags()) {
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


    PublishQueueEvent *newRamEvent(const char *eventName, const char *eventData, PublishFlags flags);

    void writeQueueToFiles();

    PublishQueueEvent *readQueueFile(int fileNum);

    void clearQueues();

    void checkQueueLimits();

    void setPausePublishing(bool value) { pausePublishing = value; }

    size_t getNumEvents();

    void lock() { os_mutex_recursive_lock(mutex); };
    bool tryLock() { return os_mutex_recursive_trylock(mutex); };
    void unlock() { os_mutex_recursive_unlock(mutex); };

    static const uint32_t FILE_MAGIC = 0x31b67663;
    static const uint8_t FILE_VERSION = 1;

protected:
    PublishQueuePosix();
    virtual ~PublishQueuePosix();

    /**
     * @brief This class is not copyable
     */
    PublishQueuePosix(const PublishQueuePosix&) = delete;

    /**
     * @brief This class is not copyable
     */
    PublishQueuePosix& operator=(const PublishQueuePosix&) = delete;

    void publishCompleteCallback(bool succeeded, const char *eventName, const char *eventData);

    void stateConnectWait();
    void stateWait();
    void statePublishWait();

    SequentialFile fileQueue;
    size_t ramQueueSize = 2;
    size_t fileQueueSize = 100;

    os_mutex_recursive_t mutex;
    std::deque<PublishQueueEvent*> ramQueue;

    PublishQueueEvent *curEvent = 0;
    int curFileNum = 0;
    unsigned long stateTime = 0;
    unsigned long durationMs = 0;
    bool publishComplete = false;
    bool publishSuccess = false;
    bool pausePublishing = false;

    unsigned long waitAfterConnect = 2000;
    unsigned long waitBetweenPublish = 1000;
    unsigned long waitAfterFailure = 30000;

    std::function<void(PublishQueuePosix&)> stateHandler = 0;

    static void systemEventHandler(system_event_t event, int param);

    static PublishQueuePosix *_instance;
};

#endif /* __PUBLISHQUEUEPOSIXRK_H */
