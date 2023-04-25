#pragma once
#ifndef MICA_NETWORK_ENSO_IMPL_H_
#define MICA_NETWORK_ENSO_IMPL_H_

namespace mica {
namespace network {
  Enso::Enso(const ::mica::util::Config& config) : config_(config),
    endpoint_count_(0), next_udp_port_number_(1), started_(false) {
    // Parse MAC address
    if (config_.get("mac_addr").exists()) {
      mac_addr_ = NetworkAddress::parse_mac_addr(
        config_.get("mac_addr").get_str().c_str());
    }
    else {
      std::cerr << "Error: MAC address must be specified." << std::endl;
      assert(false);
    }
    // Parse IPv4 address
    if (config_.get("ipv4_addr").exists()) {
      ipv4_addr_ = NetworkAddress::parse_ipv4_addr(
        config_.get("ipv4_addr").get_str().c_str());
    }
    else {
      std::cerr << "Error: IPv4 address must be specified." << std::endl;
      assert(false);
    }
    // Parse NUMA node ID
    if (config_.get("numa_id").exists()) {
      numa_id_ = config_.get("numa_id").get_uint64();
    }
    else {
      std::cerr << "Error: NIC's NUMA ID must be specified." << std::endl;
      assert(false);
    }

    // Parse endpoints configuration
    auto endpoints_conf = config_.get("endpoints");
    if (endpoints_conf.exists()) {
      assert(endpoints_conf.size() <= kMaxEndpointCount);
      for (size_t i = 0; i < endpoints_conf.size(); i++) {
        uint16_t lcore_id = ::mica::util::safe_cast<uint16_t>(
            endpoints_conf.get(i).get(0).get_uint64());
        add_endpoint(lcore_id);
      }
    } else {
      std::cerr << "Error: One or more endpoints must be specified." << std::endl;
      assert(false);
    }
  }

  void Enso::add_endpoint(uint16_t lcore_id) {
    uint16_t eid = endpoint_count_++;
    assert(eid < kMaxEndpointCount);
    assert(lcore_id < kMaxLCoreId);

    // Lcore mapping
    auto& ei = endpoint_info_[eid];
    ei.owner_lcore_id = lcore_id;

    // Stats
    ei.rx_bursts = 0;
    ei.tx_bursts = 0;
    ei.rx_packets = 0;
    ei.tx_packets = 0;
    ei.tx_dropped = 0;

    // Flow ID
    ei.numa_id = numa_id_;
    ei.mac_addr = mac_addr_;
    ei.ipv4_addr = ipv4_addr_;
    ei.udp_port = next_udp_port_number_++;
  }

  std::vector<Enso::EndpointId>
  Enso::get_endpoints() const {
    std::vector<EndpointId> eids;
    eids.reserve(endpoint_count_);
    for (uint16_t eid = 0; eid < endpoint_count_; eid++) eids.push_back(eid);
    return eids;
  }

  typename Enso::EndpointInfo&
  Enso::get_endpoint_info(EndpointId eid) {
    assert(eid < endpoint_count_);
    return endpoint_info_[eid];
  }

  const typename Enso::EndpointInfo&
  Enso::get_endpoint_info(EndpointId eid) const {
    assert(eid < endpoint_count_);
    return endpoint_info_[eid];
  }

  void Enso::start() {
      assert(!started_);
      started_ = true;
  }

  void Enso::stop() {
    assert(started_);
    started_ = false;
  }
}
}

#endif // MICA_NETWORK_ENSO_IMPL_H_
