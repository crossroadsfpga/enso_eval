#include "mica/datagram/datagram_client.h"
#include "mica/util/lcore.h"
#include "mica/util/hash.h"
#include "mica/util/zipf.h"

#include <iostream>
#include <csignal>

typedef ::mica::alloc::HugeTLBFS_SHM Alloc;

struct DPDKConfig : public ::mica::network::BasicDPDKConfig {
  static constexpr bool kVerbose = true;
};

struct DatagramClientConfig
    : public ::mica::datagram::BasicDatagramClientConfig {
  // typedef ::mica::network::DPDK<DPDKConfig> Network;
  // static constexpr bool kSkipRX = true;
  // static constexpr bool kIgnoreServerPartition = true;
  // static constexpr bool kVerbose = true;
};

typedef ::mica::datagram::DatagramClient<DatagramClientConfig> Client;

typedef ::mica::table::Result Result;

template <typename T>
static uint64_t hash(const T* key, size_t key_length) {
  return ::mica::util::hash(key, key_length);
}

class ResponseHandler
    : public ::mica::datagram::ResponseHandlerInterface<Client> {
 public:
  void handle(Client::RequestDescriptor rd, Result result, const char* value,
              size_t value_length, const Argument& arg) {
    (void)rd;
    (void)result;
    (void)value;
    (void)value_length;
    (void)arg;
  }
};

struct Args {
  uint16_t lcore_id;
  ::mica::util::Config* config;
  Alloc* alloc;
  Client* client;
  double zipf_theta;
} __attribute__((aligned(128)));

volatile bool quit;
void signal_handler(int signum) {
  if (signum == SIGINT || signum == SIGTERM) {
    printf("\n\nSignal %i (%s) received, preparing to exit...\n",
           signum, strsignal(signum));
    quit = true;
  }
}

int worker_proc(void* arg) {
  auto args = reinterpret_cast<Args*>(arg);

  Client& client = *args->client;

  ::mica::util::lcore.pin_thread(args->lcore_id);

  printf("worker running on lcore %" PRIu16 "\n", args->lcore_id);

  client.probe_reachability(quit);

  ResponseHandler rh;

  size_t num_items = 192 * 1048576;

  // double get_ratio = 0.95;
  double get_ratio = 0.50;

  uint32_t get_threshold = (uint32_t)(get_ratio * (double)((uint32_t)-1));

  ::mica::util::Rand op_type_rand(static_cast<uint64_t>(args->lcore_id) + 1000);
  ::mica::util::ZipfGen zg(num_items, args->zipf_theta,
                           static_cast<uint64_t>(args->lcore_id));
  ::mica::util::Stopwatch sw;
  sw.init_start();
  sw.init_end();

  uint64_t key_i;
  uint64_t key_hash;
  size_t key_length = sizeof(key_i);
  char* key = reinterpret_cast<char*>(&key_i);

  uint64_t value_i;
  size_t value_length = sizeof(value_i);
  char* value = reinterpret_cast<char*>(&value_i);

  bool use_noop = false;
  // bool use_noop = true;

  uint64_t last_handle_response_time = sw.now();
  // Check the response after sending some requests.
  // Ideally, packets per batch for both RX and TX should be similar.
  uint64_t response_check_interval = 20 * sw.c_1_usec();

  uint64_t seq = 0;
  while (!quit) {
    // Determine the operation type.
    uint32_t op_r = op_type_rand.next_u32();
    bool is_get = op_r <= get_threshold;

    // Generate the key.
    key_i = zg.next();
    key_hash = hash(key, key_length);

    uint64_t now = sw.now();
    while (!client.can_request(key_hash) ||
           sw.diff_in_cycles(now, last_handle_response_time) >=
               response_check_interval) {
      last_handle_response_time = now;
      client.handle_response(rh);
    }

    if (!use_noop) {
      if (is_get)
        client.get(key_hash, key, key_length);
      else {
        value_i = seq;
        client.set(key_hash, key, key_length, value, value_length, true);
      }
    } else {
      if (is_get)
        client.noop_read(key_hash, key, key_length);
      else {
        value_i = seq;
        client.noop_write(key_hash, key, key_length, value, value_length);
      }
    }

    seq++;
  }

  return 0;
}

int main(int argc, const char* argv[]) {
  if (argc != 3) {
    printf("%s NCORES ZIPF-THETA\n", argv[0]);
    return EXIT_FAILURE;
  }

  double zipf_theta = atof(argv[2]);

  ::mica::util::lcore.pin_thread(0);

  auto config = ::mica::util::Config::load_file("netbench.json");

  Alloc alloc(config.get("alloc"));

  DatagramClientConfig::Network network(config.get("network"));
  network.start();

  Client::DirectoryClient dir_client(config.get("dir_client"));

  Client client(config.get("client"), &network, &dir_client);
  client.discover_servers();

  #ifdef USE_ENSO
  uint16_t lcore_count = atoi(argv[1]);
  #else
  uint16_t lcore_count =
      static_cast<uint16_t>(::mica::util::lcore.lcore_count());

  // Reset stats.
  rte_eth_stats_reset(0);
  rte_eth_xstats_reset(0);
  #endif // USE_ENSO

  quit = false;
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  std::vector<Args> args(lcore_count);
  for (uint16_t lcore_id = 0; lcore_id < lcore_count; lcore_id++) {
    args[lcore_id].lcore_id = lcore_id;
    args[lcore_id].config = &config;
    args[lcore_id].alloc = &alloc;
    args[lcore_id].client = &client;
    args[lcore_id].zipf_theta = zipf_theta;
  }

  #ifdef USE_ENSO
  std::vector<std::thread> workers(lcore_count);
  for (uint16_t id = 1; id < lcore_count; id++) {
    workers[id] = std::thread(worker_proc, &args[id]);
  }
  worker_proc(&args[0]);

  for (size_t id = 1; id < lcore_count; id++) {
    workers[id].join();
  }
  #else
  for (uint16_t lcore_id = 1; lcore_id < lcore_count; lcore_id++) {
    if (!rte_lcore_is_enabled(static_cast<uint8_t>(lcore_id))) continue;
    rte_eal_remote_launch(worker_proc, &args[lcore_id], lcore_id);
  }
  worker_proc(&args[0]);

  struct rte_eth_stats stats;
  rte_eth_stats_get(0, &stats);

  printf("\n==== Statistics ====\n");
  printf("Port %" PRIu8 "\n", 0);
  printf("    ipackets: %" PRIu64 "\n", stats.ipackets);
  printf("    opackets: %" PRIu64 "\n", stats.opackets);
  printf("    ibytes: %" PRIu64 "\n", stats.ibytes);
  printf("    obytes: %" PRIu64 "\n", stats.obytes);
  printf("    imissed: %" PRIu64 "\n", stats.imissed);
  printf("    oerrors: %" PRIu64 "\n", stats.oerrors);
  printf("    rx_nombuf: %" PRIu64 "\n", stats.rx_nombuf);
  printf("\n");

  printf("\n==== Extended Statistics ====\n");
  int num_xstats = rte_eth_xstats_get(0, NULL, 0);
  struct rte_eth_xstat xstats[num_xstats];
  if (rte_eth_xstats_get(0, xstats, num_xstats) != num_xstats) {
    printf("Cannot get xstats\n");
  }
  struct rte_eth_xstat_name xstats_names[num_xstats];
  if (rte_eth_xstats_get_names(0, xstats_names, num_xstats) !=
      num_xstats) {
    printf("Cannot get xstats\n");
  }
  for(int i = 0; i < num_xstats; ++i) {
    printf("%s: %" PRIu64 "\n", xstats_names[i].name, xstats[i].value);
  }
  printf("\n");
  #endif // USE_ENSO

  network.stop();

  return EXIT_SUCCESS;
}
