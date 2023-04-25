#pragma once
#ifndef MICA_NETWORK_ENSO_H_
#define MICA_NETWORK_ENSO_H_

#include <iostream>

#include "enso/consts.h"
#include "enso/pipe.h"
#include <rte_ethdev.h>
#include "mica/util/config.h"
#include "mica/util/lcore.h"
#include "mica/util/safe_cast.h"
#include "mica/network/network_addr.h"

namespace mica {
namespace network {

/**
 * Represents the Enso I/O interface.
 */
class Enso {
public:
  static constexpr uint16_t kRXBurst = 1024;
  static constexpr uint16_t kTXBurst = 1024;

  static constexpr uint16_t kMaxLCoreId = 64;
  static constexpr uint16_t kMaxEndpointCount = 256;

  static constexpr uint16_t kLogCacheLineSize = 6;
  static constexpr uint16_t kCacheLineSize = (1 << kLogCacheLineSize);

  typedef uint32_t EndpointId;
  static constexpr EndpointId kInvalidEndpointId =
      std::numeric_limits<EndpointId>::max();

  typedef uint8_t PacketBuffer;

  struct EndpointInfo {
  public:
    uint16_t owner_lcore_id;

    volatile uint64_t rx_bursts;
    volatile uint64_t rx_packets;

    volatile uint64_t tx_bursts;
    volatile uint64_t tx_packets;
    volatile uint64_t tx_dropped;

    rte_ether_addr mac_addr; // MAC address
    uint32_t ipv4_addr; // IPv4 address
    uint16_t numa_id; // NUMA node ID
    uint16_t udp_port; // UDP port
  } __attribute__((aligned(kCacheLineSize)));

  std::vector<EndpointId> get_endpoints() const;
  EndpointInfo& get_endpoint_info(EndpointId eid);
  const EndpointInfo& get_endpoint_info(EndpointId eid) const;

  uint16_t get_numa_id() const { return numa_id_; }
  uint32_t get_ip_addr() const { return ipv4_addr_; }
  rte_ether_addr get_mac_addr() const { return mac_addr_; }

  explicit Enso(const ::mica::util::Config& config);
  ~Enso() {}

  void start();
  void stop();

private:
  ::mica::util::Config config_;
  void add_endpoint(uint16_t lcore_id);

  // Network device data
  uint16_t numa_id_; // NUMA node ID
  uint32_t ipv4_addr_; // IPv4 address
  rte_ether_addr mac_addr_; // MAC address

  // Endpoints
  uint16_t endpoint_count_;
  std::array<EndpointInfo, kMaxEndpointCount> endpoint_info_{};

  // Next available UDP port number
  uint16_t next_udp_port_number_;

  bool started_;
};

} // namespace network
} // namespace mica

#include "mica/network/enso/enso_impl.h"

#endif // MICA_NETWORK_ENSO_H_
