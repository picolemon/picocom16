#!/bin/bash
echo "----------------------------"
echo "Building bin"
echo "----------------------------"

baseDir="$(pwd)/$(dirname "$0")"

mkdir build
cd build
cmake .. -D CMAKE_BUILD_TYPE=Debug
make 
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Build failed"
    exit $retVal
fi

cd $baseDir