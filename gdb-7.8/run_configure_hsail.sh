#!/bin/bash

# High level logic of the hsail-gdb build system
#
# The run_configure_hsail script calls GDB's autotools configure system
# with additional parameters to enable hsail debugging.
#
# The run_configure_hsail also generates a run_make_hsail.sh which
# includes the proper environment to call  gdb's underlying make

if (($# != 1))
then
	echo "The HSAIL-GDB configure script takes only one argument"
	echo "smash: Clean up config cache"
	echo "debug: Use debug version of Debug Facilities in (amd/HwDbgFacilities) and build gdb with debug symbols"
	echo "release: Use release version of Debug Facilities in (amd/HwDbgFacilities) and build gdb for release"
	exit -1
fi

# Smash the old build otherwise it complains about config.cache
if (( $# == 1))
then
if [[ $1 == 'smash' ]]
then
	bash run_clean_cache.sh
fi
fi

hwdbgfct_opt=""

# Takes the name of the debug facilities library name as argument
# Generates a "run_make_hsail.sh" script that runs the Makefile with the env var that
# specifies the debug facilities library
#
# For internal users, they should rename amd-gdb to gdb if they are debugging the executable itself.
# This is because GDB detects the inferior
# filename and changes the command prompt to (top-gdb) when it detects that itself is being debugged.
# Thats why the original  name is important
GenerateMakeScript()
{
	touch run_make_hsail.sh
	echo "# HSAIL-GDB Makefile wrapper script" > run_make_hsail.sh
	echo "# This file Should not be checked in" >> run_make_hsail.sh
	echo "# Do not edit manually, will be overwritten when you call run_configure_hsail.sh" >> run_make_hsail.sh
	echo "# " >> run_make_hsail.sh
	echo "# Call HwDbgFacilities make" >> run_make_hsail.sh
	echo "cd ./amd/HwDbgFacilities/" >> run_make_hsail.sh
	echo "make$hwdbgfct_opt" >> run_make_hsail.sh
	echo "cd ../../" >> run_make_hsail.sh
	echo "# Set the name of the facilities library we will use" >> run_make_hsail.sh
	echo "export AMD_DEBUG_FACILITIES='$1'" >> run_make_hsail.sh
	echo "# Call GDB's make" >> run_make_hsail.sh
	echo "make" >> run_make_hsail.sh
	echo "# Rename the GDB executable so the hsail-gdb script can run it" >> run_make_hsail.sh
	echo "mv gdb/gdb gdb/amd-gdb" >> run_make_hsail.sh
	chmod +x run_make_hsail.sh
}

# <<<<Build Option 1>>>>>
# With debug symbols of GDB and debug version of Debug facilities
if (( $# == 1))
then
if [[ $1 == 'debug' ]]
then
./configure --enable-hsail --with-python \
 CFLAGS=' -g  -I../amd/HwDbgFacilities/include -I../amd/include -fPIC '\
 LDFLAGS=' -L../lib/x86_64 -pthread'
 hwdbgfct_opt=" -e HSAIL_build=debug"
 GenerateMakeScript -lAMDHwDbgFacilities-x64-d
fi
fi

# <<<<Build Option 2>>>>>
# With release version of GDB and release version of debug facilities
if (( $# == 1))
then
if [[ $1 == 'release' ]]
then
./configure --enable-hsail \
 CFLAGS=' -I../amd/HwDbgFacilities/include -I../amd/include -fPIC '\
 LDFLAGS=' -L../lib/x86_64 -pthread'
 GenerateMakeScript -lAMDHwDbgFacilities-x64
fi
fi
