#include "Particle.h"

#include "PublishQueuePosixRK.h"

SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO, { // Logging level for non-application messages
	{ "app.pubq", LOG_LEVEL_TRACE },
	{ "app.seqfile", LOG_LEVEL_TRACE }
});

int counter = 0;
size_t numToSetInSetup = 4;

void publishCounter(bool withAck);
void publishPaddedCounter(int size);

void setup() {
	// For testing purposes, wait 10 seconds before continuing to allow serial to connect
	// before doing PublishQueue setup so the debug log messages can be read.
	waitFor(Serial.isConnected, 10000);
    delay(1000);
    
	PublishQueuePosix::instance().setup();

    for(size_t ii = 0; ii < numToSetInSetup; ii++) {
        publishCounter(true);
    }
}

void loop() {
    PublishQueuePosix::instance().loop();
}

void publishCounter(bool withAck) {
	Log.info("publishing counter=%d", counter);

	char buf[32];
	snprintf(buf, sizeof(buf), "%d", counter++);
	PublishQueuePosix::instance().publish("testEvent", buf, 50, (withAck ? (PRIVATE | WITH_ACK) : PRIVATE));
}

void publishPaddedCounter(int size) {
	Log.info("publishing padded counter=%d size=%d", counter, size);

	char buf[256];
	snprintf(buf, sizeof(buf), "%05d", counter++);

	if (size > 0) {
		if (size > (int)(sizeof(buf) - 1)) {
			size = (int)(sizeof(buf) - 1);
		}

		char c = 'A';
		for(size_t ii = strlen(buf); ii < (size_t)size; ii++) {
			buf[ii] = c;
			if (++c > 'Z') {
				c = 'A';
			}
		}
		buf[size] = 0;
	}

	PublishQueuePosix::instance().publish("testEvent", buf, PRIVATE | WITH_ACK);
}
