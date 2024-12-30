#!/bin/bash

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")"; pwd)

PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../../../../"; pwd)

META_SERVER_PATH="$PROJECT_ROOT/meta/output/bin/Meta"
EXAMPLE_CLIENT_PATH="$PROJECT_ROOT/meta_example/build/example_project"
META_SERVER_PORT=8000

if [ ! -f $META_SERVER_PATH ]; then
    echo "Error: Meta server executable not found at $META_SERVER_PATH" >&2
    exit 1
fi

if [ ! -f $EXAMPLE_CLIENT_PATH ]; then
    echo "Error: Example client executable not found at $EXAMPLE_CLIENT_PATH" >&2
    exit 1
fi

if lsof -i :$META_SERVER_PORT -t >/dev/null ; then
    echo "Port $META_SERVER_PORT is already in use. Attempting to kill the process..."
    lsof -i :$META_SERVER_PORT -t | xargs kill -9 || { echo "Failed to free port $META_SERVER_PORT"; exit 1; }
fi

echo "Starting Meta server..."
$META_SERVER_PATH &
META_PID=$!

sleep 3

if ps -p $META_PID > /dev/null
then
   echo "Meta server started successfully with PID: $META_PID"
else
   echo "Error: Failed to start Meta server." >&2
   exit 1
fi

echo "Running example client..."
$EXAMPLE_CLIENT_PATH

if [ $? -ne 0 ]; then
    echo "Error: Example client failed to run or communicate with the Meta server." >&2
    kill $META_PID
    exit 1
fi

echo "Stopping Meta server..."
kill $META_PID

echo "Validation complete."
