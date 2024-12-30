#!/usr/bin/env bash

if [ $# -lt 1 ] ; then
    echo "usage: $(basename $0) PROPS_FILE [ARGS]" >&2
    exit 2
fi

source funcs.sh $1
shift

setCP || exit 1

myCP="../lib/postgres/opengauss-jdbc-5.0.0.jar:../lib/*:../dist/*"

java -cp "$myCP" -Dprop=$PROPS LoadData $*
