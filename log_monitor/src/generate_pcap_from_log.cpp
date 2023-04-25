/*
 * Copyright (c) 2022, Carnegie Mellon University
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *      * Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <arpa/inet.h>
#include <netinet/ether.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <pcap/pcap.h>
#include <sys/time.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

constexpr uint32_t kMaxPktSize = 1518;
constexpr uint32_t kPktSize = kMaxPktSize;
constexpr char kDstMac[] = "aa:aa:aa:aa:aa:aa";
constexpr char kSrcMac[] = "bb:bb:bb:bb:bb:bb";

constexpr static uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (((uint32_t)a) << 24) | (((uint32_t)b) << 16) | (((uint32_t)c) << 8) |
         ((uint32_t)d);
}

int main(int argc, char const* argv[]) {
  if (argc != 5) {
    std::cerr << "Usage: " << argv[0] << " NB_SRC NB_DST LOG_FILE "
              << "OUTPUT_PCAP" << std::endl;
    exit(1);
  }

  const int nb_src = std::stoi(argv[1]);
  const int nb_dst = std::stoi(argv[2]);
  const std::string log_filename = argv[3];
  const std::string output_pcap = argv[4];

  const int nb_streams = nb_dst;

  uint64_t buffer_size;
  char* buf;

  std::vector<std::string> lines;
  {
    std::ifstream log_file(log_filename);
    log_file.seekg(0, std::ios::end);
    buffer_size = log_file.tellg();
    log_file.seekg(0, std::ios::beg);

    buf = new char[buffer_size];
    log_file.read(buf, buffer_size);
  }

  std::cout << "buffer_size: " << buffer_size << std::endl;

  uint64_t line_begin = 0;
  for (uint64_t i = 0; i < buffer_size; ++i) {
    if (buf[i] == '\n') {
      std::string line(buf, line_begin, i - line_begin + 1);
      lines.push_back(line);
      line_begin = i + 1;
    }
  }

  delete[] buf;

  uint64_t line_offset = lines.size() / nb_streams;

  // For buf i, start from line i * line_offset and then wrap around.
  // This ensures that the streams are not synchronized.
  std::vector<char*> buffers;
  for (int i = 0; i < nb_streams; i++) {
    buf = new char[buffer_size];
    buffers.push_back(buf);
    uint64_t offset = 0;
    for (uint64_t j = 0; j < lines.size(); ++j) {
      uint64_t line_idx = (i * line_offset + j) % lines.size();
      std::string line = lines[line_idx];
      std::memcpy(buffers[i] + offset, line.c_str(), line.size());
      offset += line.size();
    }
  }

  struct ether_addr dst_mac = *ether_aton(kDstMac);
  struct ether_addr src_mac = *ether_aton(kSrcMac);

  pcap_t* pd;
  pcap_dumper_t* pdumper;

  pd = pcap_open_dead(DLT_EN10MB, 65535);
  pdumper = pcap_dump_open(pd, output_pcap.c_str());
  struct timeval ts;
  ts.tv_sec = 0;
  ts.tv_usec = 0;
  uint8_t pkt[kMaxPktSize];
  memset(pkt, 0, kMaxPktSize);

  struct ether_header* l2_hdr = (struct ether_header*)&pkt;
  struct iphdr* l3_hdr = (struct iphdr*)(l2_hdr + 1);
  struct udphdr* l4_hdr = (struct udphdr*)(l3_hdr + 1);

  memcpy(&l2_hdr->ether_dhost, &dst_mac, sizeof(dst_mac));
  memcpy(&l2_hdr->ether_shost, &src_mac, sizeof(src_mac));

  l2_hdr->ether_type = htons(ETHERTYPE_IP);
  l3_hdr->ihl = 5;
  l3_hdr->version = 4;
  l3_hdr->tos = 0;
  l3_hdr->id = 0;
  l3_hdr->frag_off = 0;
  l3_hdr->ttl = 255;
  l3_hdr->protocol = IPPROTO_UDP;

  struct pcap_pkthdr pkt_hdr;
  pkt_hdr.ts = ts;

  uint32_t src_ip = ip(192, 168, 0, 0);
  uint32_t dst_ip = ip(192, 168, 0, 0);

  uint32_t mss =
      kPktSize - sizeof(*l2_hdr) - sizeof(*l3_hdr) - sizeof(*l4_hdr) - 4;

  for (uint64_t i = 0; i < buffer_size; i += mss) {
    for (int j = 0; j < nb_dst; ++j) {
      l3_hdr->daddr = htonl(dst_ip + (uint32_t)j);
      uint32_t src_offset = j / (nb_dst / nb_src);
      l3_hdr->saddr = htonl(src_ip + src_offset);

      uint32_t payload_size = std::min((uint64_t)mss, buffer_size - i);

      l3_hdr->tot_len = htons(payload_size + sizeof(*l3_hdr) + sizeof(*l4_hdr));

      pkt_hdr.len =
          sizeof(*l2_hdr) + sizeof(*l3_hdr) + sizeof(*l4_hdr) + payload_size;
      pkt_hdr.caplen = pkt_hdr.len;

      l4_hdr->dest = htons(80);
      l4_hdr->source = htons(8080);
      l4_hdr->len = htons(sizeof(*l4_hdr) + payload_size);

      uint8_t* payload = (uint8_t*)(l4_hdr + 1);
      memcpy(payload, buffers[j] + i, payload_size);

      ++(ts.tv_usec);
      pcap_dump((u_char*)pdumper, &pkt_hdr, pkt);
    }
  }

  pcap_close(pd);
  pcap_dump_close(pdumper);

  // Free buffers.
  for (int i = 0; i < nb_streams; i++) {
    delete[] buffers[i];
  }

  return 0;
}
