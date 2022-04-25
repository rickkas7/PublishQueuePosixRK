#!/bin/bash

TEMPDIR="../PublishQueuePosixRK-Temp"
DIRS=("test" "docs")

mkdir $TEMPDIR || set status 0

for d in "${DIRS[@]}"; do
    echo $d
    mv $d $TEMPDIR
done

particle library upload
# particle library publish

mv $TEMPDIR/* .
rmdir $TEMPDIR
