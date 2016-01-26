# HSA-Debugger-GDB-Source-AMD
The HSA-Debugger-GDB-Source-AMD repository includes the source code for AMD hsail-gdb. Hsail-gdb is a modified version of GDB 7.8 that supports debugging HSAIL kernels on AMD platforms.

# Package Contents
The HSA-Debugger-GDB-Source-AMD repository includes
- A modified version of gdb-7.8 to support HSAIL debugging. Note the main hsail specific files are located in *gdb-7.8/gdb* with the hsail-* prefix.
- The AMD debug facilities library located in *amd/HwDbgFacilities/*. This library provides symbol processing for HSA kernels.

# Build Steps
1. Clone the HSA-Debugger-GDB-Source-AMD repository
  * `git clone https://github.com/HSAFoundation/HSA-Debugger-GDB-Source-AMD.git`
2. The gdb build has been modified with new files and configure settings to enable HSAIL debugging. The scripts below should be run to compile gdb.
The *run_configure_hsail* script calls the GNU autotools configure with additional parameters.
  * `./run_configure_hsail.sh debug`
3. The `run_configure_hsail.sh` script also generates the *run_make_hsail.sh* which sets environment variables for the Make step
  * `./run_make_hsail.sh`

# Running hsail-gdb
The `run_make_hsail.sh` script builds the gdb executable.

To run the hsail debugger, you'd also need to get the [AMD GPU Debug SDK](https://github.com/HSAFoundation/HSA-Debugger-Source-AMD).

Before running the hsail debugger, the *LD_LIBRARY_PATH* should include paths to
- The AMD GPU Debug Agent library built in the AMD GPU Kernel Debug SDK (located in *HSA-Debugger-Source-AMD/lib/x86_64*)
- The AMD GPU Kernel Debugging library binary shippped with the AMD GPU Kernel Debug SDK (located in *HSA-Debugger-Source-AMD/lib/x86_64*)
- Before running hsail-gdb, please update your .gdbinit file  with text in *HSA-Debugger-Source-AMD\src\HSADebugAgent\gdbinit*. The hsailConfigure function in the ~/.gdbinit sets up gdb internals for supporting HSAIL kernel debug.
- The gdb executable should be run from within the *hsail-gdb-local* script. The AMD HSA runtime requires certain environment variables to enable kernel debugging and this is set up by the *hsail-gdb-local* script.
```
./hsail-gdb-local <HSA sample application>
```
- [A brief tutorial on how to debug HSA applications using hsail-gdb](https://github.com/HSAFoundation/HSA-Debugger-AMD/blob/master/TUTORIAL.md)
