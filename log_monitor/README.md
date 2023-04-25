# Log Monitor

Here are the steps required to setup, compile, and run the log monitor.

## Setup
### Install Dependencies (Hyperscan)

Start by installing the hyperscan dependencies.

```bash
sudo apt update
sudo apt install ragel libboost-dev cmake ninja-build libpcap-dev
```

Now compile hyperscan.

```bash
mkdir -p external
cd external
git clone https://github.com/intel/hyperscan.git
cd hyperscan
mkdir build
cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
ninja
```

And install it.

```bash
sudo ninja install
```

### Extract regex from fail2ban

Start by cloning the fail2ban repository to a suitable location:

```bash
git clone git@github.com:fail2ban/fail2ban.git
```

To extract regex:

```bash
log_monitor/tools/extract_fail2ban_regex.py <fail2ban path>/config/filter.d log_monitor/tools/log_regex.txt
```

### Debian package statistics

https://popcon.debian.org/source/by_inst

To check the top packages with fail2ban regex:
```bash
grep -f log_monitor/fail2ban_services.txt log_monitor/package_statistics.txt | less
```

```
19    pam                            840730 571097 204385 62121  3127
47    openssh                        573107 357918 155871 37213 22105
134   apache2                        311861 221065 42922 19088 28786
185   libselinux                     246257 198812 25773 19400  2272
200   nginx                          237018 136825 61494 21044 17655
229   exim4                          213906 172571  7279  3578 30478
262   libssh2                        207699 132362 42939 13123 19275
371   mysql-defaults                 176277 39309 26229  9525 101214
472   libssh                         144256 18361 14847  2889 108159
1023  libkf5ksieve                   67282  2845 20340  6603 37494
1152  gnome-system-monitor           53862  4467 39306 10071    18
1196  postfix                        51090 34033 14049  1218  1790
1293  apache-pom                     45443  3194  9837  1470 30942
1357  libmail-sendmail-perl          41543  1550 38429  1546    18
1427  libdbd-mysql-perl              37365  1590  3475    25 32275
1451  kwallet-pam                    36186  2579   389  1011 32207
1474  soupsieve                      35538  2200 28795  4523    20
1525  apache-log4j1.2                33735   336  1561   116 31722
1535  monitoring-plugins             33345 15370 12000   561  5414
1537  sshfs-fuse                     33294 15678 15416  2191     9
1543  dovecot                        32942 16610 14507  1796    29
1674  libapache-poi-java             28013     0     6     0 28007
1717  mysql-8.0                      26648   587  3232   391 22438
1863  mate-system-monitor            22995  2011 18097  2860    27
2041  sendmail                       18789 12277  2974   190  3348
2057  ksshaskpass                    18487  1419 13138  3925     5
2087  spamassassin                   17975  7397 10146   425     7
2291  pim-sieve-editor               14532   773 10344  3411     4

pam-generic
sshd
apache
selinux
nginx
exim
mysql
sieve
postfix
dovecot
```

## Compile

You can compile both the DPDK and Ensō versions using CMake.

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -D CMAKE_C_COMPILER=gcc-9 -D CMAKE_CXX_COMPILER=g++-9 ..
make
```

## Run

After compiling, you should have both the Ensō and DPDK versions of the log monitor. For each run, you need to specify a regular expression file to use. We include regular expression files for different services extracted from fail2ban. You can find them in the [`log_regex`](log_regex) directory.

### Ensō

To run the Ensō version of the log monitor you must specify the number of cores, the number of streams per core, and the regular expression file to use:

```bash
sudo ./bin/enso_log_monitor <number of cores> <number of streams> <regex_file>
```

### DPDK

Running the DPDK version also requires that you specify some DPDK-specific options. For example:

```bash
sudo ./bin/dpdk_log_monitor -l 0-<nb_cores-1> -m 4 -a <NIC pcie address> -- <regex_file> --q-per-core <queues_per_core> --nb-streams <nb_streams>
```
