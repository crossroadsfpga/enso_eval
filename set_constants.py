#!/usr/bin/env python3
# Use this script to automatically set helper environment variables based on
# the `experiment_config.toml` file.

from pathlib import Path

import click
import tomli


def set_constants(config_file: Path, print_shell_vars: bool = False) -> dict:
    """Set constants for the experiments."""

    with open(config_file, "rb") as f:
        config = tomli.load(f)

    DUT_HOSTNAME = config["hosts"]["dut"]
    PKTGEN_HOSTNAME = config["hosts"]["pktgen"]

    if len(DUT_HOSTNAME) == 0:
        raise RuntimeError(f"Must define hosts.dut in {config_file}")

    if len(PKTGEN_HOSTNAME) == 0:
        raise RuntimeError(f"Must define hosts.pktgen in {config_file}")

    if print_shell_vars:
        print(f"export DUT_HOSTNAME={DUT_HOSTNAME}")
        print(f"export PKTGEN_HOSTNAME={PKTGEN_HOSTNAME}")

    PKTGEN_ENSO_EVAL_PATH = config["paths"]["pktgen_path"]
    PKTGEN_ENSO_PATH = f"{PKTGEN_ENSO_EVAL_PATH}/enso"
    config["paths"]["pktgen_enso_path"] = PKTGEN_ENSO_PATH

    if len(PKTGEN_ENSO_EVAL_PATH) == 0:
        raise RuntimeError(f"Must define paths.pktgen_path in {config_file}")

    if print_shell_vars:
        print(f"export PKTGEN_ENSO_EVAL_PATH={PKTGEN_ENSO_EVAL_PATH}")
        print(f"export PKTGEN_ENSO_PATH={PKTGEN_ENSO_PATH}")

    DUT_ENSO_EVAL_PATH = config["paths"]["dut_path"]
    DUT_ENSO_PATH = f"{DUT_ENSO_EVAL_PATH}/enso"
    config["paths"]["dut_enso_path"] = DUT_ENSO_PATH

    if len(DUT_ENSO_EVAL_PATH) == 0:
        raise RuntimeError(f"Must define paths.dut_path in {config_file}")

    if print_shell_vars:
        print(f"export DUT_ENSO_EVAL_PATH={DUT_ENSO_EVAL_PATH}")
        print(f"export DUT_ENSO_PATH={DUT_ENSO_PATH}")

    RUN_ENSO_ECHO_CMD = f"sudo {DUT_ENSO_PATH}/build/software/examples/echo"
    config["paths"]["dut_enso_echo_cmd"] = RUN_ENSO_ECHO_CMD

    RUN_ENSO_ECHO_PREFETCH_CMD = (
        f"sudo {DUT_ENSO_PATH}/build/software/examples/echo_prefetch"
    )
    config["paths"]["dut_enso_echo_prefetch_cmd"] = RUN_ENSO_ECHO_PREFETCH_CMD

    RUN_DPDK_ECHO_CMD = (
        f"sudo {DUT_ENSO_EVAL_PATH}/dpdk_echo/build_release/bin/dpdk_echo"
    )
    config["paths"]["dut_dpdk_echo_cmd"] = RUN_DPDK_ECHO_CMD

    RUN_DUT_SETUP_CMD = f"{DUT_ENSO_EVAL_PATH}/dut_setup.sh"
    config["paths"]["dut_setup_cmd"] = RUN_DUT_SETUP_CMD

    MAGLEV_PATH = f"{DUT_ENSO_EVAL_PATH}/maglev/build_release/bin"
    ENSO_MAGLEV_CMD = f"sudo {MAGLEV_PATH}/enso_maglev"
    DPDK_MAGLEV_CMD = f"sudo {MAGLEV_PATH}/dpdk_maglev"
    config["paths"]["enso_maglev_cmd"] = ENSO_MAGLEV_CMD
    config["paths"]["dpdk_maglev_cmd"] = DPDK_MAGLEV_CMD

    TOOLS_PATH = f"{DUT_ENSO_EVAL_PATH}/tools"
    CHANGE_DDIO_CMD = f"sudo {TOOLS_PATH}/ddio-bench/change-ddio"
    config["paths"]["change_ddio_cmd"] = CHANGE_DDIO_CMD

    ENSO_DUT_PCIE_ADDR = config["devices"]["enso_dut_pcie"]
    ENSO_DUT_FPGA_ID = config["devices"]["dut_fpga_id"]

    if len(ENSO_DUT_PCIE_ADDR) == 0:
        raise RuntimeError(
            f"Must define devices.enso_dut_pcie in {config_file}"
        )

    if len(ENSO_DUT_FPGA_ID) == 0:
        raise RuntimeError(f"Must define devices.dut_fpga_id in {config_file}")

    if print_shell_vars:
        print(f"export ENSO_DUT_PCIE_ADDR={ENSO_DUT_PCIE_ADDR}")
        print(f"export ENSO_DUT_FPGA_ID={ENSO_DUT_FPGA_ID}")

    DPDK_MEM_CHANNELS = config["extra"]["dpdk_mem_channels"]
    DPDK_DUT_PCIE_ADDR = config["devices"]["dpdk_dut_pcie"]

    if print_shell_vars:
        print(f"export DPDK_MEM_CHANNELS={DPDK_MEM_CHANNELS}")
        print(f"export DPDK_DUT_PCIE_ADDR={DPDK_DUT_PCIE_ADDR}")

    # We use Ensō Pktgen (at PKTGEN_HOSTNAME) to test both Ensō and DPDK.
    PKTGEN_PCIE_ADDR_ENSO = config["devices"]["enso_pktgen_pcie"]
    PKTGEN_PCIE_ADDR_DPDK = config["devices"]["dpdk_pktgen_pcie"]

    if print_shell_vars:
        print(f"export PKTGEN_PCIE_ADDR_ENSO={PKTGEN_PCIE_ADDR_ENSO}")
        print(f"export PKTGEN_PCIE_ADDR_DPDK={PKTGEN_PCIE_ADDR_DPDK}")

    PKTGEN_ENSO_FPGA_ID = config["devices"]["enso_pktgen_fpga_id"]
    PKTGEN_DPDK_FPGA_ID = config["devices"]["dpdk_pktgen_fpga_id"]

    if print_shell_vars:
        print(f"export PKTGEN_ENSO_FPGA_ID={PKTGEN_ENSO_FPGA_ID}")
        print(f"export PKTGEN_DPDK_FPGA_ID={PKTGEN_DPDK_FPGA_ID}")

    DEFAULT_NB_LLC_WAYS = config["extra"]["nb_llc_ways"]
    DEFAULT_NB_DDIO_WAYS = config["extra"]["default_nb_ddio_ways"]

    if DEFAULT_NB_LLC_WAYS is None:
        raise RuntimeError(f"Must define extra.nb_llc_ways in {config_file}")

    if DEFAULT_NB_DDIO_WAYS is None:
        raise RuntimeError(
            f"Must define extra.default_nb_ddio_ways in {config_file}"
        )

    DUT_LOG_FILE = config["logs"]["dut_log"]
    PKTGEN_LOG_FILE = config["logs"]["pktgen_log"]

    if print_shell_vars:
        print(f"export DUT_LOG_FILE={DUT_LOG_FILE}")
        print(f"export PKTGEN_LOG_FILE={PKTGEN_LOG_FILE}")

    return config


@click.command()
@click.option(
    "--config-file",
    "-c",
    type=click.Path(exists=True),
    default="experiment_config.toml",
    show_default=True,
    help="Path to config file.",
)
def main(config_file) -> None:
    set_constants(config_file, print_shell_vars=True)


if __name__ == "__main__":
    main()
