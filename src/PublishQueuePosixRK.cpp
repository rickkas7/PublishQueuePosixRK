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

void PublishQueuePosix::setup() {
    os_mutex_recursive_create(&mutex);

    // Register a system reset handler
    System.on(reset | cloud_status, systemEventHandler);

    // Start the background publish thread
    BackgroundPublish::instance().start();

    fileQueue.scanDir();

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

        _log.trace("fileNum=%d size=%u", fileNum, sb.st_size);

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
            _log.trace("readQueueFile %d bad magic=%08x version=%u headerSize=%u nameLen=%u", fileNum, hdr.magic, hdr.version, hdr.headerSize, hdr.nameLen);
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

}

void PublishQueuePosix::checkQueueLimits() {
    WITH_LOCK(*this) {
        if (ramQueue.size() > ramQueueSize) {
            // RAM queue is too large, move all to files
            writeQueueToFiles();
        }

        while(fileQueue.getQueueLen() > fileQueueSize) {
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

        _log.trace("publishing %s event=%s data=%s", curEvent ? "file" : "ram", curEvent->eventName, curEvent->eventData);

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
        else {
            // Was from the RAM-based queue
            WITH_LOCK(*this) {
                ramQueue.pop_front();
            }
        }
        delete curEvent;
        curEvent = NULL;

        durationMs = waitBetweenPublish;
    }
    else {
        // Wait and retry
        _log.trace("publish failed %d", curFileNum);
        durationMs = waitAfterFailure;
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

