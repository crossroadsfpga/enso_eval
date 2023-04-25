# Maglev Load Balancer

## Building

```bash
mkdir build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make
```

## Running

### Ensō

To run the Ensō version of Maglev, run the following command from within the `build_release` directory:

```bash
sudo ./bin/enso_maglev -l 0-<nb_cores-1> -- <nb_cores> <queues_per_core> <nb_backends>
```

For example, to run with 2 cores, 4 queues per core and 1000 backends, run:

```bash
sudo ./bin/enso_maglev -l 0-1 -- 2 4 1000
```

Also note that Maglev uses hash of the 5-tuple to direct packets to cores. Remember to set the number of fallback queues on the NIC depending on the number of cores and queues per core. For example, if you are running with 2 cores and 4 queues per core, set the number of fallback queues to 8 using the `enso` command:

```bash
enso <enso path> --host <host> --fpga <fpga id> --fallback-queues 8
```

You may also set the number of fallback queues using the JTAG console. Refer to the [docs](https://crossroadsfpga.github.io/enso/primitives/rx_enso_pipe/#binding-and-flow-steering) for more information.

### DPDK

To run the DPDK version of Maglev, first do the things required by DPDK, such as setup hugepages and bind the NIC to DPDK. Then run the following command from within the `build_release` directory:

```bash
sudo ./bin/dpdk_maglev -l 0-<nb_cores-1> -m 4 -a <NIC pcie address> -- --q-per-core <queues_per_core> --nb-backends <nb_backends>
```

For example, to run with 2 cores, 4 queues per core, 1000 backends with the NIC at `0000:01:00.0`, run:

```bash
sudo ./bin/dpdk_maglev -l 0-1 -m 4 -a 0000:01:00.0 -- --q-per-core 4 --nb-backends 1000
```
