#! /bin/bash

# This script copies the libraries and header files into the LitePipeSampleApp
# project. 
#

TEST_APP=.
OPENHOME=../..

echo "Copying Libraries into project ....."

mkdir -p $TEST_APP/Libs
mkdir -p $TEST_APP/Include

cp -f $OPENHOME/LitePipe/build/*.a $TEST_APP/Libs/
cp -f $OPENHOME/ohnetMon/build/*.a $TEST_APP/Libs/
cp -f $OPENHOME/ohnet/build/Obj/Mac-x64/Release/*.a $TEST_APP/Libs/
cp -f $OPENHOME/LitePipe/dependencies/Mac-x64/openssl/lib/*.a $TEST_APP/Libs/

# Copy include files into our tree

echo "Copying Include Files into project ....."

rsync -av --include "*/" --include "*.h" --include "*.inl" --exclude "*" $OPENHOME/OhNet/Build/Include/OpenHome $TEST_APP/Include/
rsync -av --include "*/" --include "*.h" --include "*.inl" --exclude "*" $OPENHOME/OhNet/OpenHome $TEST_APP/Include/
rsync -av --include "*/" --include "*.h" --include "*.cpp" --include "*.inl" --exclude "*" $OPENHOME/LitePipe/build/Generated $TEST_APP/Include/
rsync -av --include "*/" --include "*.h" --include "*.inl" --exclude "*" $OPENHOME/LitePipe/OpenHome $TEST_APP/Include/




