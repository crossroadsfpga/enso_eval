# DPDK Echo Server

## Building

```bash
mkdir build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make
```

## Running

Before running the echo server do the things required by DPDK, such as setup hugepages and bind the NIC to DPDK. Then run the following command from within the `build_release` directory:

```bash
sudo ./bin/dpdk_echo -l 0-$nb_cores-1 -m 4 -a <NIC pcie address> -- --q-per-core $queues_per_core --nb-cycles $nb_cycles
```

For example, to run with 2 cores, 1 queue per core, and with the NIC at `0000:01:00.0`, run:

```bash
sudo ./bin/dpdk_echo -l 0-1 -m 4 -a 0000:01:00.0 -- --q-per-core 1 --nb-cycles 0
```

The program will run indefinitely until you stop it with Ctrl+C. It will then print NIC statistics.
