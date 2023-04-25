# NitroSketch

## Building

```bash
mkdir build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make
```

## Running

### Ensō

To run the Ensō version of NitroSketch, run the following command from within the `build_release` directory:

```bash
sudo ./bin/enso_nitrosketch <core id> <number of queues>
```

For example, to run it on core 0 cores and with 4 queues, run:

```bash
sudo ./bin/enso_nitrosketch 0 4
```

Also note that NitroSketch uses hash of the 5-tuple to direct packets to cores. Remember to set the number of fallback queues on the NIC to match the number that you set on the command. For example, in the example above, set the number of fallback queues to 4 using the `enso` command:

```bash
enso <enso path> --host <host> --fpga <fpga id> --fallback-queues 4
```

You may also set the number of fallback queues using the JTAG console. Refer to the [docs](https://crossroadsfpga.github.io/enso/primitives/rx_enso_pipe/#binding-and-flow-steering) for more information.

### DPDK

To run the DPDK version of NitroSketch, first do the things required by DPDK, such as setup hugepages and bind the NIC to DPDK. Then run the following command from within the `build_release` directory:

```bash
sudo ./bin/dpdk_nitrosketch -l 0 -m 4 -a <NIC pcie address> -- --q-per-core <number of queues>
```

For example, to run with 4 queues with the NIC at `0000:01:00.0`, run:

```bash
sudo ./bin/dpdk_nitrosketch -l 0 -m 4 -a 0000:01:00.0 -- --q-per-core 4
```
