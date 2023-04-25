#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ $# -ne 1 ]; then
    echo "Must specify E810 PCIe address."
    echo
    echo "Usage: $0 E810_PCIE_ADDRESS"
    echo "  E810_PCIE_ADDRESS: PCIe address of E810 NIC."
    exit 1
fi

# Enable msr kernel module.
sudo modprobe msr

# Setting scaling_governor to performance for the first 8 cores.
echo performance | sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu1/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu3/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu5/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu6/cpufreq/scaling_governor
echo performance | sudo tee /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor

# Disable NMI watchdog.
echo 0 | sudo tee /proc/sys/kernel/nmi_watchdog

# DDIO Bench.
cd $SCRIPT_DIR/tools/ddio-bench
make

# Maglev.
cd $SCRIPT_DIR/maglev
mkdir -p build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make

# Log monitor.
cd $SCRIPT_DIR/log_monitor
mkdir -p build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make

# DPDK Echo
cd $SCRIPT_DIR/dpdk_echo
mkdir -p build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make

# Bind E810 to DPDK.
sudo modprobe uio_pci_generic
sudo dpdk-devbind.py -b uio_pci_generic $1
