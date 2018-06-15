#!/bin/bash
# IMPORTANT NOTE: run in a subshell for cd commands: 
# . install.sh

DEBUG=false
TEST=false
BUILD="mpi/laptop"
while true; do
    case "$1" in
	-b | --build ) BUILD="$2"; shift 2 ;;
	-d | --debug ) DEBUG=true; shift ;;
	-t | --test  ) TEST=true; shift ;;
	* ) break ;;
    esac
done

if [ "$BUILD" = "robot" ]; then
    echo "Loading robot cmake files..."
    cp cmake_files/cmakelists_robot/CMakeLists.txt .
    cp cmake_files/cmakelists_robot/src/CMakeLists.txt src/
    cp cmake_files/cmakelists_robot/test/CMakeLists.txt test/
fi

if $DEBUG; then
    echo "Building in debug mode..."
    mkdir -p build/debug/
    cd build/debug
    if $TEST; then
	cmake -Wno-dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TEST=True ../..
    else
	cmake -Wno-dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TEST=False ../..
    fi
else
    echo "Building in release mode..."
    mkdir -p build/release
    cd build/release
    if $TEST; then
	cmake -Wno-dev -DCMAKE_BUILD_TYPE=Release -DBUILD_TEST=True ../..
    else
	cmake -Wno-dev -DCMAKE_BUILD_TYPE=Release -DBUILD_TEST=False ../..
    fi
    
fi


make && make install
cd ../..
if $TEST; then
    ./unit_tests --log_level=message --show_progress=yes
fi

# FOR OLD SL
#cp SL_code/ilc_task.c ~/robolab/barrett/src/ilc_task.c

# FOR NEW SL COPY EVERYTHING INCLUDING CMAKELIST AND README
#cp SL_code/*.c ~/sl_xeno/barrett/src/learning_control/*.c
#cp SL_code/*.h ~/sl_xeno/barrett/include/*.h
#cp CMakeLists.txt ~/sl_xeno/barrett/src/learning_control/CMakeLists.txt
#cp readme.txt ~/sl_xeno/barrett/src/learning_control/readme.txt
