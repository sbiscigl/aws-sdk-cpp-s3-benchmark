#!/bin/bash
set -e

if [ "$#" -ne 2 ]; then
  echo "Usage: ${0} SDK_VERSION BUCKET_NAME"
  exit
fi

# create test file
python create_file.py

SDK_VERSION="$1"
BUCKET_NAME="$2"

mkdir build && \
cd build && \
cmake -DCMAKE_BUILD_TYPE=Release \
  -DBENCHMARK_DOWNLOAD_DEPENDENCIES=ON \
  -DSDK_VERSION=${SDK_VERSION} \
  -DBUCKET_NAME=${BUCKET_NAME} ..

cmake --build .

./sdk_benchmark
