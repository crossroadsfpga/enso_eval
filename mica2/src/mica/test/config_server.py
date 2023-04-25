import sys

if __name__ == "__main__":
  assert len(sys.argv) == 3
  n = int(sys.argv[1])
  q = int(sys.argv[2])
  core_list = "{}".format([i for i in range(n)])

  out = ""
  out += "{\n"
  out += "  \"dir_client\": {\n"
  out += "    \"etcd_addr\": \"127.0.0.1\",\n"
  out += "    \"etcd_port\": 2379\n"
  out += "  },\n"

  out += "\n"
  out += "  \"alloc\": {\n"
  out += "    /*\"num_pages_to_free\": [1024, 1024]*/\n"
  out += "    /*\"num_pages_to_free\": [1024]*/\n"
  out += "    \"num_pages_to_free\": [1024],\n"
  out += "    \"verbose\": true\n"
  out += "  },\n"

  out += "\n"
  out += "  \"processor\": {\n"
  out += "    \"lcores\": {},\n".format(core_list)
  out += "    \"partition_count\": {},\n".format(n*2)

  out += "\n"
  out += "    \"total_size\": 12884901888,      /* 12 GiB */\n"
  out += "    \"total_item_count\": 201326592,  /* 192 Mi */\n"

  out += "\n"
  out += "    \"concurrent_read\": false,\n"
  out += "    \"concurrent_write\": false\n"
  out += "  },"

  out += "\n"
  out += "  \"network\": {\n"
  out += "    \"numa_id\": 0,\n"
  out += "    \"ipv4_addr\": \"10.60.0.1\",\n"
  out += "    \"mac_addr\": \"12:34:56:78:9A:BC\",\n"
  out += "    \"lcores\": {},\n".format(core_list)

  out += "\n"
  out += "    \"ports\": [\n"
  out += "      {\"port_id\": 0, \"ipv4_addr\": \"10.60.0.1\"}\n"
  out += "    ],\n"

  out += "\n"
  out += "    \"endpoints\": [\n"
  for lcore_id in range(n):
    for q_id in range(q):
      suffix = "" if (lcore_id == (n-1) and q_id == (q-1)) else ","
      out += "      [{}, {}]{}\n".format(lcore_id, 0, suffix)
  out += "    ],\n"

  out += "\n"
  out += "    \"dpdk_args\": [\"-n\", \"4\", \"--socket-mem=2048\"],\n"
  out += "    \"verbose\": true\n"
  out += "  },\n"

  out += "\n"
  out += "  \"server\": {\n"
  out += "    \"rebalance_interval\": 0,\n"
  out += "    \"verbose\": true\n"
  out += "  }\n"
  out += "}\n"

  with open("server.json", "w") as f:
    f.write(out)
