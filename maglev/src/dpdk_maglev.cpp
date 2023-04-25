/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <getopt.h>
#include <inttypes.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_jhash.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_vect.h>
#include <rte_version.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "maglev.hpp"

// Uncomment to disable TX path.
// #define DISABLE_TX

// Uncomment to save extended stats.
// #define SAVE_STATS

#define RX_RING_SIZE 4048
#define TX_RING_SIZE 4048

#define MIN_NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 64

#define CMD_OPT_HELP "help"
#define CMD_OPT_Q_PER_CORE "q-per-core"
#define CMD_OPT_NB_BACKENDS "nb-backends"
enum {
  /* long options mapped to short options: first long only option value must
   * be >= 256, so that it does not conflict with short options.
   */
  CMD_OPT_HELP_NUM = 256,
  CMD_OPT_SOFT_LB_NUM,
  CMD_OPT_SOFT_LB_HASH_NUM,
  CMD_OPT_Q_PER_CORE_NUM,
  CMD_OPT_NB_BACKENDS_NUM
};

static void print_usage(const char* program_name) {
  printf(
      "%s [EAL options] --"
      " [--help] |\n"
      " [--q-per-core]\n\n"

      "  --help: Show this help and exit\n"
      "  --q-per-core: Number of queues per core\n"
      "  --nb-backends: Number of backend servers\n",
      program_name);
}

/* if we ever need short options, add to this string */
static const char short_options[] = "";

static const struct option long_options[] = {
    {CMD_OPT_HELP, no_argument, NULL, CMD_OPT_HELP_NUM},
    {CMD_OPT_Q_PER_CORE, required_argument, NULL, CMD_OPT_Q_PER_CORE_NUM},
    {CMD_OPT_NB_BACKENDS, required_argument, NULL, CMD_OPT_NB_BACKENDS_NUM},
    {0, 0, 0, 0}};

struct parsed_args_t {
  uint32_t q_per_core;
  uint32_t nb_backends;
};

static int parse_args(int argc, char** argv,
                      struct parsed_args_t* parsed_args) {
  int opt;
  int long_index;

  parsed_args->q_per_core = 1;
  parsed_args->nb_backends = 1024;

  while ((opt = getopt_long(argc, argv, short_options, long_options,
                            &long_index)) != EOF) {
    switch (opt) {
      case CMD_OPT_HELP_NUM:
        return 1;
      case CMD_OPT_Q_PER_CORE_NUM:
        parsed_args->q_per_core = atoi(optarg);
        break;
      case CMD_OPT_NB_BACKENDS_NUM:
        parsed_args->nb_backends = atoi(optarg);
        break;
      default:
        return -1;
    }
  }

  return 0;
}

volatile bool quit;
static uint32_t q_per_core;
static uint32_t nb_backends;

static void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    printf("\n\nSignal %i (%s) received, preparing to exit...\n", signum,
           strsignal(signum));
    quit = true;
  }
}

/* Check if the port is on the same NUMA node as the polling thread */
__rte_always_inline void warn_if_not_same_numa(uint8_t port) {
  if (rte_eth_dev_socket_id(port) > 0 &&
      rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
    printf("Port %" PRIu8 " is on remote NUMA node\n", port);
  }
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int port_init(uint16_t port, struct rte_mempool* mbuf_pool,
                            uint16_t rx_rings, uint16_t tx_rings) {
  /* Inspired by NetBricks options */
  struct rte_eth_conf port_conf = {};
  port_conf.link_speeds = ETH_LINK_SPEED_AUTONEG; /* auto negotiate speed */
  port_conf.lpbk_mode = 0;
  port_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;  /* Use one of CDB, RSS or VMDQ */
  port_conf.txmode.mq_mode = ETH_MQ_TX_NONE; /* Disable DCB and VMDQ */
  port_conf.intr_conf = {0, 0, 0};           // Disable interrupts

  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf txconf;

  if (!rte_eth_dev_is_valid_port(port)) return -1;

  retval = rte_eth_dev_info_get(port, &dev_info);
  if (retval != 0) {
    printf("Error during getting device (port %u) info: %s\n", port,
           strerror(-retval));
    return retval;
  }

  if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
    port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

  port_conf.rx_adv_conf.rss_conf.rss_hf = dev_info.flow_type_rss_offloads;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0) return retval;

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0) return retval;

  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(
        port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) return retval;
  }

  txconf = dev_info.default_txconf;
  txconf.offloads = port_conf.txmode.offloads;

  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                    rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0) return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if (retval < 0) return retval;

  /* Display the port MAC address. */
  struct rte_ether_addr addr;
  retval = rte_eth_macaddr_get(port, &addr);
  if (retval != 0) return retval;

  printf("Port %u MAC: %02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8
         ":%02" PRIx8 ":%02" PRIx8 "\n",
         port, addr.addr_bytes[0], addr.addr_bytes[1], addr.addr_bytes[2],
         addr.addr_bytes[3], addr.addr_bytes[4], addr.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  retval = rte_eth_promiscuous_enable(port);
  if (retval != 0) return retval;

  return 0;
}

static int lcore_work(void* arg) {
  uint32_t first_queue = (uint32_t)(uint64_t)arg;
  struct rte_mbuf* bufs[BURST_SIZE];
  unsigned lcore_id;
  // struct rte_ether_addr original_src_mac;
  // uint32_t original_src_ip;
  uint32_t nb_queues = q_per_core;

  std::vector<uint32_t> backend_ips;
  uint32_t init_ip = RTE_IPV4(10, 0, 0, 1);
  for (uint32_t i = 0; i < nb_backends; ++i) {
    backend_ips.push_back(init_ip + i);
  }
  Maglev maglev(backend_ips);
  int ret = maglev.setup();
  if (ret) {
    rte_exit(EXIT_FAILURE, "Issue setting up maglev : \"%s\"\n",
             rte_strerror(ret));
  }

  uint64_t* rx_stats =
      (uint64_t*)rte_zmalloc("rx_stats", nb_queues * 8, RTE_CACHE_LINE_SIZE);
  uint64_t* tx_stats =
      (uint64_t*)rte_zmalloc("tx_stats", nb_queues * 8, RTE_CACHE_LINE_SIZE);
  uint64_t* drops =
      (uint64_t*)rte_zmalloc("drops", nb_queues * 8, RTE_CACHE_LINE_SIZE);

  lcore_id = rte_lcore_id();
  unsigned lcore_idx = first_queue / q_per_core;

  warn_if_not_same_numa(0);

  printf("Starting core %u with first queue %u\n", lcore_id, first_queue);

  /* Run until the application is quit or killed. */
  while (!quit) {
    for (uint32_t q_offset = 0; q_offset < nb_queues; ++q_offset) {
      const uint32_t queue = first_queue + q_offset;
      const uint16_t nb_rx = rte_eth_rx_burst(0, queue, bufs, BURST_SIZE);
      if (unlikely(nb_rx == 0)) {
        continue;
      }

      rx_stats[q_offset] += nb_rx;

      for (uint16_t i = 0; i < nb_rx; ++i) {
        uint8_t* buf = rte_pktmbuf_mtod_offset(bufs[i], uint8_t*, 0);
        maglev.lookup(buf);
      }

#ifdef DISABLE_TX
      uint16_t buf;
      for (buf = 0; buf < nb_rx; buf++) {
        rte_pktmbuf_free(bufs[buf]);
      }
#else
      const uint16_t nb_tx = rte_eth_tx_burst(0, queue, bufs, nb_rx);

      tx_stats[q_offset] += nb_tx;

      /* Free any unsent packets. */
      if (unlikely(nb_tx < nb_rx)) {
        uint16_t buf;
        drops[q_offset] += nb_rx - nb_tx;
        for (buf = nb_tx; buf < nb_rx; buf++) {
          rte_pktmbuf_free(bufs[buf]);
        }
      }
#endif  // DISABLE_TX
    }
  }

  // Random sleep to avoid output mangling
  struct timespec tv {
    0, lcore_idx * 1000
  };
  nanosleep(&tv, NULL);

  uint64_t total_tx = 0;
  uint64_t total_rx = 0;
  uint64_t total_drops = 0;
  for (uint32_t q_offset = 0; q_offset < nb_queues; ++q_offset) {
    uint32_t queue = first_queue + q_offset;
    printf("core %u (queue %u): rx: %lu, tx: %lu, drops: %lu\n", lcore_id,
           queue, rx_stats[q_offset], tx_stats[q_offset], drops[q_offset]);
    total_rx += rx_stats[q_offset];
    total_tx += tx_stats[q_offset];
    total_drops += drops[q_offset];
  }

  return 0;
}

struct ring_pair {
  struct rte_ring* rx;
  struct rte_ring* tx;
};

int main(int argc, char* argv[]) {
  struct rte_mempool* mbuf_pool;
  unsigned nb_ports;
  struct parsed_args_t parsed_args;
  uint16_t port_id = 0;  // Using only port 0.

  quit = false;
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* Initialize the Environment Abstraction Layer (EAL). */
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }
  argc -= ret;
  argv += ret;

  std::cout << "Using DPDK version " << rte_version() << std::endl;

  ret = parse_args(argc, argv, &parsed_args);
  if (ret) {
    print_usage(argv[0]);
    if (ret == 1) {
      return 0;
    }
    rte_exit(EXIT_FAILURE, "Invalid CLI options\n");
  }

  q_per_core = parsed_args.q_per_core;
  nb_backends = parsed_args.nb_backends;

  nb_ports = rte_eth_dev_count_avail();
  if (nb_ports != 1) {
    rte_exit(EXIT_FAILURE, "Error: support only for one port\n");
  }

  uint16_t lcore_count = rte_lcore_count();

  printf("Using %u cores\n", lcore_count);

  unsigned mbuf_entries = nb_ports * lcore_count * q_per_core * RX_RING_SIZE +
                          nb_ports * lcore_count * q_per_core * BURST_SIZE +
                          nb_ports * lcore_count * q_per_core * TX_RING_SIZE +
                          lcore_count * q_per_core * MBUF_CACHE_SIZE;

  mbuf_entries = RTE_MAX(mbuf_entries, (unsigned)MIN_NUM_MBUFS);
  /* Creates a new mempool in memory to hold the mbufs. */
  mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_POOL", mbuf_entries, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (mbuf_pool == NULL) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

  /* Initialize all ports. */
  if (port_init(port_id, mbuf_pool, lcore_count * q_per_core,
                lcore_count * q_per_core))
    rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu16 "\n", 0);

  // Reset stats.
  rte_eth_stats_reset(port_id);
  rte_eth_xstats_reset(port_id);

  unsigned lcore_id;

  uint64_t queue = q_per_core;
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    rte_eal_remote_launch(lcore_work, (void*)queue, lcore_id);
    queue += q_per_core;
  }

  lcore_work(0);

  rte_eal_mp_wait_lcore();

  struct rte_eth_stats stats;
  rte_eth_stats_get(port_id, &stats);

#ifdef SAVE_STATS
  FILE* my_stdout = freopen("out.txt", "w", stdout);
  assert(my_stdout != NULL);
#endif  // SAVE_STATS

  printf("\n==== Statistics ====\n");
  printf("Port %" PRIu8 "\n", port_id);
  printf("    ipackets: %" PRIu64 "\n", stats.ipackets);
  printf("    opackets: %" PRIu64 "\n", stats.opackets);
  printf("    ibytes: %" PRIu64 "\n", stats.ibytes);
  printf("    obytes: %" PRIu64 "\n", stats.obytes);
  printf("    imissed: %" PRIu64 "\n", stats.imissed);
  printf("    oerrors: %" PRIu64 "\n", stats.oerrors);
  printf("    rx_nombuf: %" PRIu64 "\n", stats.rx_nombuf);
  printf("\n");

  printf("\n==== Extended Statistics ====\n");
  int num_xstats = rte_eth_xstats_get(port_id, NULL, 0);
  struct rte_eth_xstat xstats[num_xstats];
  if (rte_eth_xstats_get(port_id, xstats, num_xstats) != num_xstats) {
    printf("Cannot get xstats\n");
  }
  struct rte_eth_xstat_name xstats_names[num_xstats];
  if (rte_eth_xstats_get_names(port_id, xstats_names, num_xstats) !=
      num_xstats) {
    printf("Cannot get xstats\n");
  }
  for (int i = 0; i < num_xstats; ++i) {
    printf("%s: %" PRIu64 "\n", xstats_names[i].name, xstats[i].value);
  }
  printf("\n");

#ifdef SAVE_STATS
  fclose(stdout);
#endif  // SAVE_STATS

  return 0;
}
