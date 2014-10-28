#! /bin/bash
DIRECTORY=$1
TARGET=$2
ARCHIVE=$DIRECTORY.tar.gz
tar -czf $ARCHIVE $DIRECTORY
scp $ARCHIVE $TARGET/$ARCHIVE
ssh ${TARGET%:*} "cd ${TARGET#*:} && rm -rf $DIRECTORY && tar -xzf $ARCHIVE \
	&& rm $ARCHIVE"
rm $ARCHIVE
