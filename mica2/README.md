# MICA 2

A fast in-memory key-value store adapted from [efficient/mica2](https://github.com/efficient/mica2). Ported to be compatible with an uplifted DPDK version (20.11 LTS), as well as the high-performance [Ensō](https://github.com/crossroadsfpga/enso) streaming interface.

## Building
1. Install dependencies. This port is known to work with `Ubuntu 16.04+`, `g++ 9.0+`, `Python 3.6+`, `CMake v3.5+`, `DPDK 20.11.7`, `etcd v2.3.3+`, and `Ensō 0.3`. Other versions of these packages are untested, but may still work.

2. To build the networked client and server applications, in the root `mica2` directory, run:
   ```bash
   mkdir build && cd build
   cmake ..
   make -j
   ```
    If the compilation was successful, you should see four executables in the `build` directory: `server_enso`, `server_dpdk`, `client_enso`, and `client_dpdk`.

## Running

Assuming that you are ssh'ing from a local machine into a remote DUT-style setup, for this step you may find it convenient to open four terminal windows side-by-side. We will use two of those terminals to start/stop etcd instances on the two remote machines, while the other two terminals will be used to run the MICA server and client applications, respectively.

### Common Setup
1. *Generate the JSON configuration files for the MICA server and client*. On the **server** machine, from the root `mica2` directory, run the following command:
   ```bash
   cd build
   cp src/mica/test/config_server.py .
   python3 config_server.py 1 1 # Args: SERVER_NUM_CORES, SERVER_ENDPOINTS_PER_CORE
   cd ..
   ```
   The first argument to `config_server.py` indicates the number of cores that the MICA server should be allocated, while the second argument indicates the number of endpoints per core. You may sweep `SERVER_NUM_CORES` (to evaluate how K/V store throughput scales with core count), but we recommend keeping `SERVER_ENDPOINTS_PER_CORE` fixed to 1 (yields the highest throughput for both Ensō and DPDK). At this point, the `mica2/build` directory should contain a file called `server.json` representing the MICA server configuration.

   Similarly, on the **client** machine, from the root `mica2` directory, run:
   ```bash
   cd build
   cp src/mica/test/config_client.py .
   python3 config_client.py 2 1 # Args: CLIENT_NUM_CORES, CLIENT_ENDPOINTS_PER_CORE
   cd ..
   ```
   The arguments to `config_server.py` follow the same format as described above. We recommend setting `CLIENT_NUM_CORES` to `2 * SERVER_NUM_CORES` to ensure that the client (traffic generator) is not the bottleneck. As before, `CLIENT_ENDPOINTS_PER_CORE` can be fixed to 1. The above snippet of code creates a file called `netbench.json` in the `mica2/build` directory representing the MICA client configuration.

2. *Start etcd instances on both the server and client machines*. On the **server** machine, start an etcd cluster which the client can connect to. This can be achieved via a command that looks like this:
   ```bash
   sudo killall etcd; rm -r server.etcd; etcd --name server                                 \
       --initial-advertise-peer-urls http://${SERVER_IP}:2380                               \
       --listen-peer-urls http://${SERVER_IP}:2380                                          \
       --listen-client-urls http://${SERVER_IP}:2379,http://127.0.0.1:2379                  \
       --advertise-client-urls http://${SERVER_IP}:2379 --initial-cluster-token cluster-1   \
       --initial-cluster client=http://${CLIENT_IP}:2380,server=http://${SERVER_IP}:2380    \
       --initial-cluster-state new
   ```

   Similarly, on the **client** machine, start an etcd instance to connect to the server:
   ```bash
   sudo killall etcd; rm -r client.etcd; etcd --name client                                 \
       --initial-advertise-peer-urls http://${CLIENT_IP}:2380                               \
       --listen-peer-urls http://${CLIENT_IP}:2380                                          \
       --listen-client-urls http://${CLIENT_IP}:2379,http://127.0.0.1:2379                  \
       --advertise-client-urls http://${CLIENT_IP}:2379 --initial-cluster-token cluster-1   \
       --initial-cluster client=http://${CLIENT_IP}:2380,server=http://${SERVER_IP}:2380    \
       --initial-cluster-state new
   ```
   where `${SERVER_IP}` and `${CLIENT_IP}` should be replaced by the appropriate server and client IP addresses, respectively. If successful, both instances should produce output that looks like this:
   ```bash
   ...
   etcdserver: set the initial cluster version to 2.3
   ```

### Running with Ensō

3. If you are using the Ensō-based versions of the server or client (or both), you must first run the `enso` command on the corresponding machine(s) as described [here](https://crossroadsfpga.github.io/enso/running/#loading-the-bitstream-and-configuring-the-nic).

### Running the MICA Client and Server

4. On the **server** machine, from the `mica2/build` directory, do: `sudo ./server_enso` to run the Ensō-based MICA server, or `sudo ./server_dpdk` to run the DPDK-based MICA server.

5. Similarly, on the **client** machine, from the `mica2/build` directory, do: `sudo ./client_enso ${CLIENT_NUM_CORES} 0` to run the Ensō-based MICA client, or `sudo ./client_dpdk ${CLIENT_NUM_CORES} 0` to run the DPDK-based MICA client.

**Important**: If you are using the DPDK versions of either binary, you will need to supply standard DPDK EAL arguments as well. The command for the DPDK-based executable will look something like this:
```bash
sudo ./bin/server_dpdk -l 0-${CLIENT_NUM_CORES-1} -m 4 -- ${CLIENT_NUM_CORES} 0
```

You may stop the server or client applications at any time by pressing `Ctrl+C`.

## License

    Copyright 2014, 2015, 2016, 2017 Carnegie Mellon University

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

Authors: Hyeontaek Lim (hl@cs.cmu.edu)
