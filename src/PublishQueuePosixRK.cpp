#include "PublishQueuePosixRK.h"

#include "BackgroundPublishRK.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

PublishQueuePosix *PublishQueuePosix::_instance;

static Logger _log("app.pubq");


PublishQueuePosix &PublishQueuePosix::instance() {
    if (!_instance) {
        _instance = new PublishQueuePosix();
    }
    return *_instance;
}

PublishQueuePosix &PublishQueuePosix::withRamQueueSize(size_t size) { 
    ramQueueSize = size;

    if (stateHandler) {
        _log.trace("withRamQueueSize(%u)", ramQueueSize);
        checkQueueLimits();
    }
    return *this; 
}


PublishQueuePosix &PublishQueuePosix::withFileQueueSize(size_t size) {
    fileQueueSize = size; 

    if (stateHandler) {
        _log.trace("withFileQueueSize(%u)", fileQueueSize);
        checkQueueLimits();
    }
    return *this; 
}

void PublishQueuePosix::setup() {
    if (system_thread_get_state(nullptr) != spark::feature::ENABLED) {
        _log.error("SYSTEM_THREAD(ENABLED) is required");
        return;
    }

    os_mutex_recursive_create(&mutex);

    // Register a system reset handler
    System.on(reset | cloud_status, systemEventHandler);

    // Start the background publish thread
    BackgroundPublish::instance().start();

    fileQueue.scanDir();

    checkQueueLimits();

    stateHandler = &PublishQueuePosix::stateConnectWait;
}

void PublishQueuePosix::loop() {
    if (stateHandler) {
        stateHandler(*this);
    }
}

bool PublishQueuePosix::publishCommon(const char *eventName, const char *eventData, int ttl, PublishFlags flags1, PublishFlags flags2) {

    PublishQueueEvent *event = newRamEvent(eventName, eventData, flags1 | flags2);
    if (!event) {
        return false;
    }
    _log.trace("publishCommon eventName=%s eventData=%s", eventName, eventData ? eventData : "");

    WITH_LOCK(*this) {
        ramQueue.push_back(event);

        _log.trace("fileQueueLen=%u ramQueueLen=%u connected=%d", fileQueue.getQueueLen(), ramQueue.size(), Particle.connected());

        if (fileQueue.getQueueLen() == 0 && (ramQueue.size() <= ramQueueSize) && Particle.connected()) {
            // No files in the disk-based queue, RAM-based queue is not full, and we are cloud connected
            // Leave the event in the RAM queue and return true
            _log.trace("queued to ramQueue");
        }
        else {
            // We need to move the queue to the file system
            writeQueueToFiles();
        }
        checkQueueLimits();
    }


    return true;
}

PublishQueueEvent *PublishQueuePosix::newRamEvent(const char *eventName, const char *eventData, PublishFlags flags) {

    if (!eventData) {
        eventData = "";
    }
    if (strlen(eventName) > particle::protocol::MAX_EVENT_NAME_LENGTH) {
        return NULL;
    }
    if (strlen(eventData) > particle::protocol::MAX_EVENT_DATA_LENGTH) {
        return NULL;
    }

    PublishQueueEvent *event;

    event = (PublishQueueEvent *) new char[sizeof(PublishQueueEvent) + strlen(eventData)];
    if (event) {
        event->flags = flags;
        strcpy(event->eventName, eventName);
        strcpy(event->eventData, eventData);
    }
    return event;
}

void PublishQueuePosix::writeQueueToFiles() {

    WITH_LOCK(*this) {
        while(!ramQueue.empty()) {
            PublishQueueEvent *event = ramQueue.front();
            ramQueue.pop_front();

            int fileNum = fileQueue.reserveFile();

            int fd = open(fileQueue.getPathForFileNum(fileNum), O_RDWR | O_CREAT);
            if (fd) {
                PublishQueueFileHeader hdr;
                hdr.magic = FILE_MAGIC;
                hdr.version = FILE_VERSION;
                hdr.headerSize = sizeof(PublishQueueFileHeader);
                hdr.nameLen = sizeof(PublishQueueEvent::eventName);
                write(fd, &hdr, sizeof(hdr));

                write(fd, event, sizeof(PublishQueueEvent) + strlen(event->eventData));
                close(fd);

                // This message is monitored by the automated test tool. If you edit this, change that too.
                _log.trace("writeQueueToFiles fileNum=%d", fileNum);
            }
            fileQueue.addFileToQueue(fileNum);

            delete event;
        }
    }
}


PublishQueueEvent *PublishQueuePosix::readQueueFile(int fileNum) {
    PublishQueueEvent *result = NULL;

    int fd = open(fileQueue.getPathForFileNum(fileNum), O_RDONLY);
    if (fd) {
        struct stat sb;
        fstat(fd, &sb);

        _log.trace("fileNum=%d size=%ld", fileNum, sb.st_size);

        PublishQueueFileHeader hdr;
        
        lseek(fd, 0, SEEK_SET);
        read(fd, &hdr, sizeof(PublishQueueFileHeader));
        if (sb.st_size >= (off_t)(sizeof(PublishQueueFileHeader) + sizeof(PublishQueueEvent)) &&
            hdr.magic == FILE_MAGIC && 
            hdr.version == FILE_VERSION &&
            hdr.headerSize == sizeof(PublishQueueFileHeader) &&
            hdr.nameLen == sizeof(PublishQueueEvent::eventName)) {

            size_t eventSize = sb.st_size - sizeof(PublishQueueFileHeader);

            result = (PublishQueueEvent *)new char[eventSize];
            if (result) {
                read(fd, result, eventSize);

                if (((char *)result)[eventSize - 1] == 0 && strlen(result->eventName) < (sizeof(PublishQueueEvent::eventName) - 1)) {
                    _log.trace("readQueueFile %d event=%s data=%s", fileNum, result->eventName, result->eventData);
                }
                else {
                    _log.trace("readQueueFile %d corrupted event name or data", fileNum);
                    delete result;
                    result = NULL;
                }

            }
        } else {
            _log.trace("readQueueFile %d bad magic=%08lx version=%u headerSize=%u nameLen=%u", fileNum, hdr.magic, hdr.version, hdr.headerSize, hdr.nameLen);
        }

        close(fd);
    }
    return result;
}

void PublishQueuePosix::clearQueues() {
    WITH_LOCK(*this) {
        while(!ramQueue.empty()) {
            PublishQueueEvent *event = ramQueue.front();
            ramQueue.pop_front();

            delete event;
        }

        fileQueue.removeAll(true);
    }

    _log.trace("clearQueues");
}

void PublishQueuePosix::checkQueueLimits() {
    WITH_LOCK(*this) {
        if (ramQueue.size() > ramQueueSize) {
            // RAM queue is too large, move all to files
            writeQueueToFiles();
        }

        while(fileQueue.getQueueLen() > (int)fileQueueSize) {
            int fileNum = fileQueue.getFileFromQueue(true);
            if (fileNum) {
                fileQueue.removeFileNum(fileNum, false);
                _log.info("discarded event %d", fileNum);
            }
        }
    }
}

size_t PublishQueuePosix::getNumEvents() {
    size_t result = 0;

    WITH_LOCK(*this) {
        result = ramQueue.size();
        if (result == 0) {
            result = fileQueue.getQueueLen();

            if (curEvent && curFileNum == 0) {
                // This happens when we are sending an event from the RAM queue
                // It's not in the RAM queue, but we want to count it, because
                // otherwise getNumEvents would return 1 for the event sent from
                // a file (because the file is not deleted until sent) and
                // this makes the behavior consistent.
                result++;
            }
        }
    }
    return result;
}

void PublishQueuePosix::publishCompleteCallback(bool succeeded, const char *eventName, const char *eventData) {
    publishComplete = true;
    publishSuccess = succeeded;
}


void PublishQueuePosix::stateConnectWait() {
    if (Particle.connected()) {
        stateTime = millis();
        durationMs = waitAfterConnect;
        stateHandler = &PublishQueuePosix::stateWait;
    }
}


void PublishQueuePosix::stateWait() {
    if (!Particle.connected()) {
        stateHandler = &PublishQueuePosix::stateConnectWait;
        return;
    }

    if (millis() - stateTime < durationMs) {
        return;
    }

    if (pausePublishing) {
        return;
    }
    
    curFileNum = fileQueue.getFileFromQueue(false);
    if (curFileNum) {
        curEvent = readQueueFile(curFileNum);
        if (!curEvent) {
            // Probably a corrupted file, discard
            _log.info("discarding corrupted file %d", curFileNum);
            fileQueue.getFileFromQueue(true);
            fileQueue.removeFileNum(curFileNum, false);
        }
    }
    else {
        if (!ramQueue.empty()) {
            curEvent = ramQueue.front();
            ramQueue.pop_front();
        }
        else {
            curEvent = NULL;
        }
    }

    if (curEvent) {
        stateTime = millis();
        stateHandler = &PublishQueuePosix::statePublishWait;
        publishComplete = false;
        publishSuccess = false;

        // This message is monitored by the automated test tool. If you edit this, change that too.
        _log.trace("publishing %s event=%s data=%s", (curFileNum ? "file" : "ram"), curEvent->eventName, curEvent->eventData);

        if (BackgroundPublish::instance().publish(curEvent->eventName, curEvent->eventData, curEvent->flags, 
            [this](bool succeeded, const char *eventName, const char *eventData, const void *context) {
                publishCompleteCallback(succeeded, eventName, eventData);
            })) {
            // Successfully started publish
        }
    }
}
void PublishQueuePosix::statePublishWait() {
    if (!publishComplete) {
        return;
    }

    if (publishSuccess) {
        // Remove from the queue
        _log.trace("publish success %d", curFileNum);

        if (curFileNum) {
            // Was from the file-based queue
            int fileNum = fileQueue.getFileFromQueue(false);
            if (fileNum == curFileNum) {
                fileQueue.getFileFromQueue(true);
                fileQueue.removeFileNum(fileNum, false);
                _log.trace("removed file %d", fileNum);
            }
            curFileNum = 0;
        }

        delete curEvent;
        curEvent = NULL;
        durationMs = waitBetweenPublish;
    }
    else {
        // Wait and retry
        // This message is monitored by the automated test tool. If you edit this, change that too.
        _log.trace("publish failed %d", curFileNum);
        durationMs = waitAfterFailure;

        if (curFileNum) {
            // Was from the file-based queue
            delete curEvent;
            curEvent = NULL;
        }
        else {
            // Was in the RAM-based queue, put back
            WITH_LOCK(*this) {
                ramQueue.push_front(curEvent);
            }
            // Then write the entire queue to files
            _log.trace("writing to files after publish failure");
            writeQueueToFiles();
        }
    }

    stateHandler = &PublishQueuePosix::stateWait;
    stateTime = millis();
}


PublishQueuePosix::PublishQueuePosix() {
    fileQueue.withDirPath("/usr/pubqueue");
}

PublishQueuePosix::~PublishQueuePosix() {

}

void PublishQueuePosix::systemEventHandler(system_event_t event, int param) {
    if ((event == reset) || ((event == cloud_status) && (param == cloud_status_disconnecting))) {
        _log.trace("reset or disconnect event, save files to queue");
        PublishQueuePosix::instance().writeQueueToFiles();
    }
}

