import sys

if __name__ == "__main__":
  assert len(sys.argv) == 3
  n = int(sys.argv[1])
  p = str(sys.argv[2])
  core_list = "{}".format([i for i in range(n)])

  out = ""
  out += "{\n"
  out += "  \"dir_client\": {\n"
  out += "    \"etcd_addr\": \"127.0.0.1\",\n"
  out += "    \"etcd_port\": 2379\n"
  out += "  },\n"

  out += "\n"
  out += "  \"alloc\": {\n"
  out += "    \"num_pages_to_free\": [500, 500]\n"
  out += "  },\n"

  out += "\n"
  out += "  \"network\": {\n"
  out += "    \"numa_id\": 0,\n"
  out += "    \"ipv4_addr\": \"10.50.0.1\",\n"
  out += "    \"mac_addr\": \"12:34:56:78:9A:BC\",\n"
  out += "    \"lcores\": {},\n".format(core_list)

  out += "\n"
  out += "    \"ports\": [\n"
  out += "      {\"port_id\": " + p + ", \"ipv4_addr\": \"10.50.0.1\"}\n"
  out += "    ],\n"

  out += "\n"
  out += "    \"endpoints\": [\n"
  for lcore_id in range(n):
    suffix = "" if lcore_id == (n-1) else ","
    out += "      [{}, {}]{}\n".format(lcore_id, p, suffix)
  out += "    ],\n"

  out += "\n"
  out += "    \"dpdk_args\": [\"-n\", \"4\", \"--socket-mem=1000,1000\"]\n"
  out += "  },\n"

  out += "\n"
  out += "  \"client\": {}\n"
  out += "}\n"

  with open("netbench.json", "w") as f:
    f.write(out)
