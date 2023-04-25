#!/usr/bin/env python3

import asyncio
import itertools
import subprocess
import sys
import tempfile
import time

from collections import defaultdict
from pathlib import Path
from typing import Any, Optional, TextIO, Union

import click

from rich.console import Group, Console
from rich.live import Live
from rich.progress import (
    BarColumn,
    Progress,
    TextColumn,
    TimeElapsedColumn,
    TimeRemainingColumn,
)

from netexp.helpers import (
    set_host_clock,
    download_file,
    LocalHost,
    RemoteHost,
    get_host_from_hostname,
)
from netexp.pktgen.dpdk import DpdkConfig
from netexp.throughput import zero_loss_throughput

from enso.ensogen import EnsoGen
from enso.enso_nic import EnsoNic

from set_constants import set_constants

console = Console()

if sys.version_info < (3, 9, 0):
    raise RuntimeError("Python 3.9 or a more recent version is required.")


async def update_remote_repos(
    hostname_paths: dict[str, str], hostname_logs: dict[str, TextIO]
) -> None:
    """Run rsync to update the remote repos."""
    local_enso_eval_path = Path(__file__).resolve().parent

    # Run git ls-files --exclude-standard -oi --directory to get the list of
    # files to exclude and save it to a temporary file.
    excluded_files = tempfile.NamedTemporaryFile()
    get_ignore_files_cmd = [
        "git",
        "ls-files",
        "--exclude-standard",
        "-oi",
        "--directory",
    ]

    subprocess.run(
        get_ignore_files_cmd,
        stdout=excluded_files,
        stderr=subprocess.DEVNULL,
        cwd=local_enso_eval_path,
    )
    subprocess.run(
        get_ignore_files_cmd,
        stdout=excluded_files,
        stderr=subprocess.DEVNULL,
        cwd=local_enso_eval_path / "enso",
    )

    # Also ignore .git.
    excluded_files.write(b".git\n")
    excluded_files.write(b"enso/.git\n")
    excluded_files.flush()

    async def update_remote_repo(
        hostname: str, path: str, log: TextIO, excluded_files_name: str
    ) -> None:
        """Update a remote repo."""

        # Create path on remote host if it does not exist.
        host = get_host_from_hostname(hostname)
        host.run_command(f"mkdir -p {path}", print_command=log)

        rsync_cmd = [
            "rsync",
            "-rlptzv",
            "--progress",
            "--exclude-from",
            excluded_files_name,
            f"{local_enso_eval_path}/",
            f"{hostname}:{path}",
        ]

        subprocess.run(rsync_cmd, stdout=log, stderr=log)

        bitstream_path = "enso/scripts/alt_ehipc2_hw.sof"

        # Also sync the bitstream.
        rsync_cmd = [
            "rsync",
            "-rlptzv",
            "--progress",
            f"{local_enso_eval_path}/{bitstream_path}",
            f"{hostname}:{path}/{bitstream_path}",
        ]
        subprocess.run(rsync_cmd, stdout=log, stderr=log)

    update_tasks = []
    for hostname, path in hostname_paths.items():
        update_tasks.append(
            asyncio.create_task(
                update_remote_repo(
                    hostname,
                    path,
                    hostname_logs[hostname],
                    excluded_files.name,
                )
            )
        )

    for task in update_tasks:
        await task

    del excluded_files


async def setup_remote_repos(
    hostname_paths: dict[str, str], hostname_logs: dict[str, TextIO]
) -> None:
    """Run setup script in the remote repos."""

    async def run_setup(hostname: str, path: str, log: TextIO) -> None:
        host = get_host_from_hostname(hostname)
        host_setup = host.run_command(
            f"{path}/enso/setup.sh", source_bashrc=True
        )
        host_setup.watch(stdout=log, stderr=log)
        if host_setup.recv_exit_status() != 0:
            raise RuntimeError(f"{hostname} setup failed.")

        del host

    setup_tasks = []
    for hostname, path in hostname_paths.items():
        setup_tasks.append(
            asyncio.create_task(
                run_setup(hostname, path, hostname_logs[hostname])
            )
        )

    for task in setup_tasks:
        await task


def set_ddio_ways(
    host: Union[LocalHost, RemoteHost],
    device_bus: str,
    nb_ways: int,
    total_nb_ways: int,
    config: dict[str, Any],
    log_file: Union[bool, TextIO] = False,
) -> None:
    """Set the number of ways for the DDIO on a remote host.

    Args:
        ssh_client: SSH connection to the remote host.
        device_bus: PCI bus of the device (in hex).
        nb_ways: The number of DDIO ways to set (set to 0 to disable DDIO).
        total_nb_ways: The total number of DDIO ways on the remote host.
    """
    assert 0 <= nb_ways <= total_nb_ways

    enable_ddio = int(nb_ways > 0)
    device_bus = device_bus.split("0x")[-1]

    cmd = host.run_command(
        f"{config['paths']['change_ddio_cmd']} 0x{device_bus} {enable_ddio}",
        print_command=log_file,
    )
    cmd.watch(stdout=log_file, stderr=log_file)
    status = cmd.recv_exit_status()
    if status != 0:
        raise RuntimeError(f'Could not change DDIO for bus "{device_bus}"')

    if nb_ways == 0:
        return

    if nb_ways > total_nb_ways:
        raise ValueError(
            f"Cannot set number of ways to {nb_ways}, only "
            f"{total_nb_ways} available"
        )

    full_mask = (1 << total_nb_ways) - 1
    ddio_mask = (full_mask << (total_nb_ways - nb_ways)) & full_mask

    cmd = host.run_command(f"sudo wrmsr 0xc8b {hex(ddio_mask)}")
    cmd.watch(stdout=log_file, stderr=log_file)

    status = cmd.recv_exit_status()

    if status != 0:
        raise RuntimeError(
            f"Could not change the number of DDIO ways to {nb_ways}"
        )


class Dut:
    def __init__(
        self, config: dict[str, Any], log_file: Union[bool, TextIO] = False
    ) -> None:
        self.log_file = log_file
        self.config = config

    def start(self) -> None:
        raise NotImplementedError

    def restart(self) -> None:
        raise NotImplementedError

    def wait_ready(self) -> None:
        raise NotImplementedError

    def stop(self) -> None:
        raise NotImplementedError

    def wait_stop(self) -> None:
        raise NotImplementedError

    def zero_loss_throughput(
        self,
        pktgen: EnsoGen,
        pkt_size: int,
        max_throughput: int = 100_000_000_000,
        precision: int = 100_000_000,
        warmup_duration: int = 5,  # seconds.
    ) -> int:
        self.wait_ready()

        # Warmup.
        if warmup_duration > 0:
            pps = max_throughput / ((pkt_size + 20) * 8)
            nb_pkts = int(pps * warmup_duration)
            pktgen.start(max_throughput, nb_pkts)
            pktgen.wait_transmission_done()

        throughput = zero_loss_throughput(
            pktgen,
            pkt_size,
            max_throughput=max_throughput,
            precision=precision,
            target_duration=1,
            log_file=pktgen.log_file,
        )
        return throughput


class MultiCoreDut(Dut):
    def __init__(
        self,
        cpu_clock: int,
        pcie_device_addr: str,
        config: dict[str, Any],
        nb_llc_ways: Optional[int] = None,
        nb_ddio_ways: Optional[int] = None,
        **kwargs: Any,
    ) -> None:
        super().__init__(config=config, **kwargs)

        self.core_clocks = {}
        self.last_ddio_setting = {}
        self.cpu_clock = cpu_clock
        self.pcie_device_addr = pcie_device_addr

        if nb_llc_ways is None:
            nb_llc_ways = self.config["extra"]["nb_llc_ways"]
            if nb_llc_ways is None:
                raise ValueError(
                    "nb_llc_ways must be set in the configuration"
                )

        if nb_ddio_ways is None:
            nb_ddio_ways = self.config["extra"]["default_nb_ddio_ways"]
            if nb_ddio_ways is None:
                raise ValueError(
                    "default_nb_ddio_ways must be set in the configuration"
                )

        self.nb_llc_ways = nb_llc_ways
        self.nb_ddio_ways = nb_ddio_ways

    def start(
        self, nb_cores: int, queues_per_core: int = 1, nb_cycles: int = 0
    ) -> None:
        raise NotImplementedError

    def set_cpu_clock(self, clk_frequency: int) -> None:
        self.cpu_clock = clk_frequency

    def set_ddio_ways(self, nb_ways: int) -> None:
        self.nb_ddio_ways = nb_ways
        self.apply_ddio_ways()

    @property
    def host(self) -> Union[LocalHost, RemoteHost]:
        raise NotImplementedError

    def get_hostname(self) -> str:
        raise NotImplementedError

    def apply_clock_to_cores(self, nb_cores: int) -> None:
        wait_for_clock = False

        for i in range(nb_cores):
            if self.core_clocks.get(i, None) != self.cpu_clock:
                set_host_clock(self.host, self.cpu_clock, [i])
                self.core_clocks[i] = self.cpu_clock
                wait_for_clock = True

        # HACK(sadok): For some unknown reason running a command right after
        # setting the clock leads to a slight a performance degradation.
        if wait_for_clock:
            time.sleep(5)

    def apply_ddio_ways(self) -> None:
        # This should work for 0000:17:00.0 and 17:00.0 formats.
        device_bus = self.pcie_device_addr.split(":")[-2]

        if self.last_ddio_setting.get(device_bus, None) == self.nb_ddio_ways:
            return

        set_ddio_ways(
            self.host,
            device_bus,
            self.nb_ddio_ways,
            self.nb_llc_ways,
            config=self.config,
            log_file=self.log_file,
        )
        self.last_ddio_setting[device_bus] = self.nb_ddio_ways


class EnsoEchoDut(MultiCoreDut):
    def __init__(
        self,
        nic: EnsoNic,
        pcie_device_addr: str,
        config: dict[str, Any],
        notif_per_pkt: bool = False,
        cpu_clock: int = 0,
        many_dsc_queues: bool = False,
        verbose: bool = False,
        cmd: Optional[str] = None,
        **kwargs: Any,
    ) -> None:
        super().__init__(cpu_clock, pcie_device_addr, config=config, **kwargs)

        self.nic = nic
        self.notif_per_pkt = notif_per_pkt
        self.many_dsc_queues = many_dsc_queues
        self.verbose = verbose
        self.sw_instance = None

        if cmd is None:
            cmd = config["paths"]["dut_enso_echo_cmd"]
        self.cmd = cmd

    def start(
        self, nb_cores: int, queues_per_core: int = 2, nb_cycles: int = 0
    ) -> None:
        if self.sw_instance is not None:
            raise RuntimeError("Program already running")

        if self.notif_per_pkt:
            self.nic.enable_desc_per_pkt()

        self.apply_clock_to_cores(nb_cores)
        self.apply_ddio_ways()

        self.sw_instance = self.nic.host.run_command(
            f"{self.cmd} {nb_cores} {queues_per_core} {nb_cycles}",
            pty=True,
            print_command=self.log_file,
        )

        self.running_nb_cores = nb_cores
        self.running_queues_per_core = queues_per_core
        self.running_nb_cycles = nb_cycles

    def restart(self) -> None:
        if self.sw_instance is None:
            raise RuntimeError("DUT not running")
        self.stop()
        self.wait_stop()
        self.start(
            self.running_nb_cores,
            self.running_queues_per_core,
            self.running_nb_cycles,
        )
        self.wait_ready()

    def wait_ready(self) -> None:
        if self.sw_instance is None:
            raise RuntimeError("Program did not start")

        self.sw_instance.watch(
            keyboard_int=self.stop,
            stdout=self.log_file,
            stderr=self.log_file,
            stop_pattern="Mbps",
        )

        # If a sw instance already finished, something wrong happened.
        if self.sw_instance.exit_status_ready():
            raise RuntimeError("Program terminated before experiment")

    def stop(self) -> None:
        if self.sw_instance is None:
            return

        self.sw_instance.send(b"\x03")  # Ctrl+C.
        self.sw_instance.watch(stdout=self.log_file, stderr=self.log_file)

        # Set clocks back to maximum.
        set_host_clock(self.host, 0, list(range(self.running_nb_cores)))

        if self.notif_per_pkt:
            self.nic.disable_desc_per_pkt()

        self.sw_instance = None

    def wait_stop(self) -> None:
        pass

    @property
    def host(self) -> Union[LocalHost, RemoteHost]:
        return self.nic.host

    def get_hostname(self) -> str:
        return self.nic.host_name


class EnsoMaglevDut(EnsoEchoDut):
    def __init__(
        self,
        nic: EnsoNic,
        pcie_device_addr: str,
        nb_backends: int,
        config: dict[str, Any],
        **kwargs: Any,
    ) -> None:
        super().__init__(nic, pcie_device_addr, config=config, **kwargs)

        self.nb_backends = nb_backends

        self.core_clocks = {}
        self.sw_instance = None

    def start(
        self, nb_cores: int, queues_per_core: int = 4, nb_cycles: int = 0
    ) -> None:
        if self.sw_instance is not None:
            raise RuntimeError("Program already running")

        # Maglev relies on hashing flows to cores.
        self.nic.fallback_queues = queues_per_core * nb_cores

        self.apply_clock_to_cores(nb_cores)

        self.sw_instance = self.host.run_command(
            f"{self.config['paths']['enso_maglev_cmd']} -l {0}-{nb_cores-1} --"
            f" {nb_cores} {queues_per_core} {self.nb_backends}",
            pty=True,
            print_command=self.log_file,
        )

        self.running_nb_cores = nb_cores
        self.running_queues_per_core = queues_per_core

    def stop(self) -> None:
        if self.sw_instance is None:
            return

        self.sw_instance.send(b"\x03")  # Ctrl+C.
        self.sw_instance.watch(stdout=self.verbose, stderr=self.verbose)

        # Set clocks back to maximum.
        set_host_clock(self.host, 0, list(range(self.running_nb_cores)))

        self.sw_instance = None


class DpdkEchoDut(MultiCoreDut):
    def __init__(
        self,
        hostname: str,
        pcie_device_addr: str,
        config: dict[str, Any],
        cpu_clock: int = 0,
        verbose: bool = False,
        **kwargs: Any,
    ) -> None:
        super().__init__(cpu_clock, pcie_device_addr, config=config, **kwargs)

        self.hostname = hostname
        self.verbose = verbose
        self.core_clocks = {}
        self.ssh_client = None
        self._host = None
        self.sw_instance = None

    def set_cpu_clock(self, cpu_clock: int) -> None:
        self.cpu_clock = cpu_clock

    def start(
        self, nb_cores: int, queues_per_core: int = 1, nb_cycles: int = 0
    ) -> None:
        if self.sw_instance is not None:
            raise RuntimeError("Program already running")

        self.running_cores = list(range(nb_cores))

        dpdk_config = DpdkConfig(
            cores=self.running_cores,
            mem_channels=self.config["extra"]["dpdk_mem_channels"],
            pci_allow_list=self.config["devices"]["dpdk_dut_pcie"],
        )

        self.apply_clock_to_cores(nb_cores)

        self.sw_instance = self.host.run_command(
            f"{self.config['paths']['dut_dpdk_echo_cmd']} {dpdk_config} -- "
            f"--q-per-core {queues_per_core} --nb-cycles {nb_cycles}",
            pty=True,
            print_command=self.log_file,
        )

        self.running_nb_cores = nb_cores
        self.running_queues_per_core = queues_per_core
        self.nb_cycles = nb_cycles

    def restart(self) -> None:
        if self.sw_instance is None:
            raise RuntimeError("DUT not running")
        self.stop()
        self.wait_stop()
        self.start(
            self.running_nb_cores, self.running_queues_per_core, self.nb_cycles
        )
        self.wait_ready()

    def wait_ready(self) -> None:
        if self.sw_instance is None:
            raise RuntimeError("Program did not start")

        self.sw_instance.watch(
            keyboard_int=self.stop,
            stop_pattern="Starting core 0 with first queue 0",
            stdout=self.log_file,
            stderr=self.log_file,
        )

        # If the sw instance already finished, something wrong happened.
        if self.sw_instance.exit_status_ready():
            raise RuntimeError("Program terminated before experiment")

    def stop(self) -> None:
        if self.sw_instance is None:
            return

        self.sw_instance.send(b"\x03")  # Ctrl+C.
        self.sw_instance.watch(stdout=self.log_file, stderr=self.log_file)

        # Set clocks back to maximum.
        set_host_clock(self.host, 0, self.running_cores)

        self.close_ssh_client()

    def wait_stop(self) -> None:
        pass

    def close_ssh_client(self) -> None:
        if self.ssh_client is None:
            return
        self.ssh_client.close()
        del self.ssh_client
        self.ssh_client = None

    @property
    def host(self) -> Union[LocalHost, RemoteHost]:
        if self._host is None:
            self._host = get_host_from_hostname(self.hostname)
        return self._host

    def get_hostname(self) -> str:
        return self.hostname


class DpdkMaglevDut(DpdkEchoDut):
    def __init__(
        self,
        hostname: str,
        pcie_device_addr: str,
        nb_backends: int,
        config: dict[str, Any],
        **kwargs: Any,
    ) -> None:
        super().__init__(hostname, pcie_device_addr, config=config, **kwargs)
        self.nb_backends = nb_backends
        self.sw_instance = None

    def start(
        self, nb_cores: int, queues_per_core: int = 1, nb_cycles: int = 0
    ) -> None:
        if self.sw_instance is not None:
            raise RuntimeError("Program already running")

        self.running_cores = list(range(nb_cores))

        dpdk_config = DpdkConfig(
            cores=self.running_cores,
            mem_channels=self.config["extra"]["dpdk_mem_channels"],
            pci_allow_list=self.config["devices"]["dpdk_dut_pcie"],
        )

        self.apply_clock_to_cores(nb_cores)

        self.sw_instance = self.host.run_command(
            f"{self.config['paths']['dpdk_maglev_cmd']} {dpdk_config} -- "
            f"--q-per-core {queues_per_core} --nb-backends {self.nb_backends}",
            pty=True,
            print_command=self.log_file,
        )

        self.running_nb_cores = nb_cores
        self.running_queues_per_core = queues_per_core


class Experiment:
    def __init__(self, name: str, iterations: int) -> None:
        self.name = name
        self.iterations = iterations

    def run(self, step_progress: Progress, current_iter: int) -> None:
        raise NotImplementedError

    def run_many(self, progress: Progress, step_progress: Progress) -> None:
        task_id = progress.add_task("", total=self.iterations, name=self.name)
        for iter in range(self.iterations):
            self.run(step_progress, iter)
            progress.update(task_id, advance=1)

        progress.update(task_id, description="[bold green] done!")


class ThroughputExperiment(Experiment):
    def __init__(
        self,
        name: str,
        iterations: int,
        save_name: Path,
        dut: MultiCoreDut,
        pktgen: EnsoGen,
        pkt_sizes: list[int],
        nb_cores: list[int],
        queues_per_core: list[int],
        cpu_clocks: list[int],
        nb_cycles: list[int],
        ddio_ways: list[int],
        precision: int = 100_000_000,
        pktgen_args: Optional[dict[str, int]] = None,
    ) -> None:
        super().__init__(name, iterations)
        self.save_name = save_name
        self.pktgen = pktgen
        self.pkt_sizes = pkt_sizes
        self.nb_cores = nb_cores
        self.queues_per_core = queues_per_core
        self.cpu_clocks = cpu_clocks
        self.nb_cycles = nb_cycles
        self.ddio_ways = ddio_ways
        self.precision = precision
        self.pktgen_args = pktgen_args or {}
        self.dut = dut

        header = (
            "pkt_size,nb_cores,queues_per_core,cpu_clock,nb_cycles,ddio_ways,"
            "precision,throughput\n"
        )

        self.experiment_tracker = defaultdict(int)

        # If file exists, continue where we left.
        if self.save_name.exists():
            with open(self.save_name) as f:
                read_header = f.readline()
                assert header == read_header
                for row in f.readlines():
                    end = row.rfind(",")
                    self.experiment_tracker[row[:end]] += 1
        else:
            with open(self.save_name, "w") as f:
                f.write(header)

    def run(self, step_progress: Progress, current_iter: int) -> None:
        experiments = list(
            itertools.product(
                self.pkt_sizes,
                self.nb_cores,
                self.queues_per_core,
                self.cpu_clocks,
                self.nb_cycles,
                self.ddio_ways,
            )
        )

        task_id = step_progress.add_task(self.name, total=len(experiments))

        for (
            pkt_size,
            cores,
            q_per_core,
            cpu_clock,
            cycles,
            ddio_way,
        ) in experiments:
            exp_str = (
                f"{pkt_size},{cores},{q_per_core},{cpu_clock},{cycles},"
                f"{ddio_way}"
            )

            exp_str_with_precision = f"{exp_str},{self.precision}"

            if self.experiment_tracker[exp_str_with_precision] > current_iter:
                console.log(f"[orange1]Skipping: {exp_str_with_precision}")
                step_progress.update(task_id, advance=1)
                continue

            step_progress.update(task_id, description=f"({exp_str})")

            if "pcap" in self.pktgen_args:
                raise NotImplementedError
            else:
                pkt_size = self.pktgen_args.get("pkt_size", pkt_size)
                nb_src = self.pktgen_args.get("nb_src", 1)
                nb_dst = self.pktgen_args.get("nb_dst", q_per_core * cores)
                self.pktgen.set_params(pkt_size, nb_src, nb_dst)

            self.dut.set_cpu_clock(cpu_clock)
            self.dut.set_ddio_ways(ddio_way)
            self.dut.start(cores, q_per_core, cycles)

            throughput = self.dut.zero_loss_throughput(
                self.pktgen, pkt_size, precision=self.precision
            )

            self.dut.stop()
            self.dut.wait_stop()

            with open(self.save_name, "a") as f:
                f.write(f"{exp_str_with_precision},{throughput}\n")

            step_progress.update(task_id, advance=1)

        step_progress.update(task_id, visible=False)


class LatencyExperiment(Experiment):
    def __init__(
        self,
        name: str,
        iterations: int,
        base_save_name: Path,
        dut: MultiCoreDut,
        pktgen: EnsoGen,
        pkt_sizes: list[int],
        nb_cores: list[int],
        queues_per_core: list[int],
        cpu_clocks: list[int],
        throughput_loads: list[int],
        target_duration: int = 5,
        always_save: bool = False,
        pktgen_args: Optional[dict[str, int]] = None,
    ) -> None:
        super().__init__(name, iterations)
        self.base_save_name = base_save_name
        self.pktgen = pktgen
        self.pkt_sizes = pkt_sizes
        self.nb_cores = nb_cores
        self.queues_per_core = queues_per_core
        self.cpu_clocks = cpu_clocks
        self.pktgen_args = pktgen_args or {}
        self.throughput_loads = throughput_loads
        self.throughput_loads.sort()
        self.target_duration = target_duration
        self.dut = dut
        self.iter_nb = 0
        self.always_save = always_save

        throughput_header = (
            "pkt_size,nb_cores,queues_per_core,cpu_clock,load,throughput\n"
        )

        if not self.base_save_name.exists():
            with open(self.base_save_name, "w") as f:
                f.write(throughput_header)

    def _get_save_file_name(
        self,
        pkt_size: int,
        cores: int,
        q_per_core: int,
        cpu_clock: int,
        load: int,
    ):
        stem = self.base_save_name.stem
        stem = f"{stem}-{pkt_size}_{cores}_{q_per_core}_{cpu_clock}_{load}"
        save_name = self.base_save_name.with_stem(stem)

        return save_name

    def run(self, step_progress: Progress, current_iter: int) -> None:
        experiments = list(
            itertools.product(
                self.pkt_sizes,
                self.nb_cores,
                self.queues_per_core,
                self.cpu_clocks,
            )
        )

        task_total = len(experiments) * len(self.throughput_loads)
        task_id = step_progress.add_task(self.name, total=task_total)

        for (pkt_size, cores, q_per_core, cpu_clock) in experiments:
            for i, load in enumerate(self.throughput_loads):
                save_file_name = self._get_save_file_name(
                    pkt_size, cores, q_per_core, cpu_clock, load
                )

                exp_str = f"{pkt_size},{cores},{q_per_core},{cpu_clock},{load}"

                if save_file_name.exists():
                    console.log(f"[orange1]Skipping: {save_file_name}")
                    step_progress.update(task_id, advance=1)
                    continue

                step_progress.update(task_id, description=f"({exp_str})")

                if "pcap" in self.pktgen_args:
                    raise NotImplementedError
                else:
                    pkt_size = self.pktgen_args.get("pkt_size", pkt_size)
                    nb_src = self.pktgen_args.get("nb_src", 1)
                    nb_dst = self.pktgen_args.get("nb_dst", q_per_core * cores)
                    self.pktgen.set_params(pkt_size, nb_src, nb_dst)

                self.dut.set_cpu_clock(cpu_clock)
                self.dut.start(cores, q_per_core)
                self.dut.wait_ready()

                pps = load / ((pkt_size + 20) * 8)
                nb_pkts = int(pps * self.target_duration)

                # Make sure RTT hist is enabled.
                og_rtt_hist = self.pktgen.rtt_hist
                self.pktgen.rtt_hist = True

                self.pktgen.clean_stats()

                self.pktgen.start(load, nb_pkts)

                save_file = True

                try:
                    self.pktgen.wait_transmission_done()
                except RuntimeError as e:
                    # HACK(sadok): Should use proper Exception class.
                    if e.args[0] != "Error running EnsōGen":
                        raise
                    save_file = False or self.always_save

                nb_rx_pkts = self.pktgen.get_nb_rx_pkts()

                self.dut.stop()

                # Restore previous configuration.
                self.pktgen.rtt_hist = og_rtt_hist
                # self.pktgen.stats_delay = og_stats_delay

                # Do not save if error occurred in EnsōGen. This includes
                # not receiving all packets back, which indicates that the DUT
                # cannot keep up with the offered load.
                if not save_file:
                    steps = len(self.throughput_loads) - i
                    step_progress.update(task_id, advance=steps)
                    break

                if nb_rx_pkts > nb_pkts:
                    raise RuntimeError(
                        "Received more packets than sent. Measurement is "
                        "unreliable."
                    )

                download_file(
                    self.pktgen.nic.host_name,
                    self.pktgen.hist_file,
                    str(save_file_name),
                    log_file=self.pktgen.log_file,
                )

                with open(self.base_save_name, "a") as f:
                    f.write(f"{exp_str},{self.pktgen.get_rx_throughput()}\n")

                step_progress.update(task_id, advance=1)

        step_progress.update(task_id, visible=False)


class MeasureDutExperiment(Experiment):
    def __init__(
        self,
        name: str,
        iterations: int,
        base_save_name: Path,
        dut: MultiCoreDut,
        pktgen: EnsoGen,
        pkt_sizes: list[int],
        nb_cores: list[int],
        queues_per_core: list[int],
        cpu_clocks: list[int],
        loads: list[int],
        target_duration: int = 20,
        pktgen_args: Optional[dict[str, int]] = None,
        verbose: bool = False,
    ) -> None:
        super().__init__(name, iterations)
        self.base_save_name = base_save_name
        self.pktgen = pktgen
        self.pkt_sizes = pkt_sizes
        self.nb_cores = nb_cores
        self.queues_per_core = queues_per_core
        self.cpu_clocks = cpu_clocks
        self.loads = loads
        self.target_duration = target_duration
        self.verbose = verbose
        self.pktgen_args = pktgen_args or {}
        self.dut = dut

        if not self.base_save_name.exists():
            with open(self.base_save_name, "w") as f:
                f.write(
                    "pkt_size,nb_cores,queues_per_core,cpu_clock,load,"
                    "throughput\n"
                )

        assert target_duration > 5

    def _get_save_file_name(
        self,
        pkt_size: int,
        cores: int,
        q_per_core: int,
        cpu_clock: int,
        load: int,
    ):
        stem = self.base_save_name.stem
        stem = f"{stem}-{pkt_size}_{cores}_{q_per_core}_{cpu_clock}_{load}"
        save_name = self.base_save_name.with_stem(stem)

        return save_name

    def run(self, step_progress: Progress, current_iter: int) -> None:
        experiments = list(
            itertools.product(
                self.pkt_sizes,
                self.nb_cores,
                self.queues_per_core,
                self.cpu_clocks,
                self.loads,
            )
        )

        task_id = step_progress.add_task(self.name, total=len(experiments))

        for (pkt_size, cores, q_per_core, cpu_clock, load) in experiments:
            save_file_name = self._get_save_file_name(
                pkt_size, cores, q_per_core, cpu_clock, load
            )

            exp_str = f"{pkt_size},{cores},{q_per_core},{cpu_clock},{load}"

            if save_file_name.exists():
                console.log(f"Skipping: {save_file_name}")
                step_progress.update(task_id, advance=1)
                continue

            step_progress.update(task_id, description=f"({exp_str})")

            self.dut.set_cpu_clock(cpu_clock)

            if "pcap" in self.pktgen_args:
                raise NotImplementedError
            else:
                pkt_size = self.pktgen_args.get("pkt_size", pkt_size)
                nb_src = self.pktgen_args.get("nb_src", 1)
                nb_dst = self.pktgen_args.get("nb_dst", q_per_core * cores)
                self.pktgen.set_params(pkt_size, nb_src, nb_dst)

            self.dut.start(cores, q_per_core)
            self.dut.wait_ready()

            self.pktgen.start(load, 0)
            time.sleep(1)

            self.measure_dut(save_file_name)

            self.pktgen.pktgen_cmd.watch(
                timeout=1,
                stdout=self.pktgen.log_file,
                stderr=self.pktgen.log_file,
            )

            self.pktgen.stop()
            self.dut.stop()

            with open(self.base_save_name, "a") as f:
                f.write(f"{exp_str},{self.pktgen.get_rx_throughput()}\n")

            step_progress.update(task_id, advance=1)

        step_progress.update(task_id, visible=False)

    def measure_dut(self, save_file_name):
        raise NotImplementedError


class ExperimentTracker:
    def __init__(self) -> None:
        self.overall_progress = Progress(
            TimeElapsedColumn(),
            BarColumn(),
            TimeRemainingColumn(),
            TextColumn("{task.description}"),
        )
        self.experiment_iters_progress = Progress(
            TextColumn("  "),
            TextColumn(
                "[bold blue]{task.fields[name]}: " "{task.percentage:.0f}%"
            ),
            BarColumn(),
            TimeRemainingColumn(),
            TextColumn("{task.description}"),
        )
        self.step_progress = Progress(
            TextColumn("  "),
            TimeElapsedColumn(),
            TextColumn("[bold purple]"),
            BarColumn(),
            TimeRemainingColumn(),
            TextColumn("{task.description}"),
        )
        self.progress_group = Group(
            Group(self.step_progress, self.experiment_iters_progress),
            self.overall_progress,
        )
        self.experiments: list[Experiment] = []

    def add_experiment(self, experiment: Experiment) -> None:
        self.experiments.append(experiment)

    def run_experiments(self):
        with Live(self.progress_group):
            nb_exps = len(self.experiments)
            overall_task_id = self.overall_progress.add_task("", total=nb_exps)

            for i, exp in enumerate(self.experiments):
                description = (
                    f"[bold #AAAAAA]({i} out of {nb_exps} experiments)"
                )
                self.overall_progress.update(
                    overall_task_id, description=description
                )
                exp.run_many(
                    self.experiment_iters_progress, self.step_progress
                )
                self.overall_progress.update(overall_task_id, advance=1)

            self.overall_progress.update(
                overall_task_id, description="[bold green] All done!"
            )


max_clock = 3100000

throughput_loads_gbps = [1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 99, 100]
throughput_loads = [load * 1_000_000_000 for load in throughput_loads_gbps]

pkt_sizes = [64, 128, 256, 512, 1024, 1518]


async def load_dut_nic(
    dut_log_file: TextIO,
    load_bitstream: bool,
    skip_config: bool,
    config: dict[str, Any],
) -> EnsoNic:
    with console.status("Loading DUT NIC"):
        dut_nic = EnsoNic(
            config["devices"]["dut_fpga_id"],
            config["paths"]["dut_enso_path"],
            host_name=config["hosts"]["dut"],
            verbose=False,
            log_file=dut_log_file,
            load_bitstream=load_bitstream,
            skip_config=skip_config,
        )
        dut_nic.get_stats()

    console.log("[green]Done loading DUT NIC")
    return dut_nic


async def load_pktgen(
    pktgen_log_file: TextIO,
    load_bitstream: bool,
    skip_config: bool,
    fpga_id: str,
    pcie_addr: str,
    config: dict[str, Any],
) -> EnsoGen:
    with console.status("Loading Pktgen NIC"):
        pktgen_nic = EnsoNic(
            fpga_id,
            config["paths"]["pktgen_enso_path"],
            host_name=config["hosts"]["pktgen"],
            verbose=False,
            log_file=pktgen_log_file,
            load_bitstream=load_bitstream,
            skip_config=skip_config,
        )

        pktgen = EnsoGen(
            pktgen_nic,
            pcie_addr=pcie_addr,
            log_file=pktgen_log_file,
        )

    console.log("[green]Done loading Pktgen NIC")
    return pktgen


async def run_setup(dut_log_file: TextIO, config: dict[str, Any]) -> None:
    with console.status("Running setup"):
        client = get_host_from_hostname(config["hosts"]["dut"])
        setup_cmd = client.run_command(
            (
                f"{config['paths']['dut_setup_cmd']} "
                f"{config['devices']['dpdk_dut_pcie']}"
            ),
            pty=True,
            print_command=dut_log_file,
        )
        setup_cmd.watch(stdout=dut_log_file, stderr=dut_log_file)
        del client
    console.log("[green]Done running setup")


async def enso_experiments(
    data_dir: Path,
    load_bitstream: bool,
    iterations: int,
    dut_log_file: TextIO,
    pktgen_log_file: TextIO,
    config: dict[str, Any],
) -> list[Experiment]:
    skip_config = not load_bitstream

    load_dut_nic_task = asyncio.create_task(
        load_dut_nic(dut_log_file, load_bitstream, skip_config, config)
    )
    load_pktgen_task = asyncio.create_task(
        load_pktgen(
            pktgen_log_file,
            load_bitstream,
            skip_config,
            config["devices"]["enso_pktgen_fpga_id"],
            config["devices"]["enso_pktgen_pcie"],
            config,
        )
    )

    dut_nic = await load_dut_nic_task

    run_setup_task = asyncio.create_task(run_setup(dut_log_file, config))

    pktgen = await load_pktgen_task
    await run_setup_task

    experiments: list[Experiment] = [
        ThroughputExperiment(
            "Ensō Maglev throughput (SYN flood)",
            iterations=iterations,
            save_name=(
                data_dir / Path("enso_maglev_throughput_1000_1048576.csv")
            ),
            dut=EnsoMaglevDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                nb_backends=1000,
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[4],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
            pktgen_args=dict(nb_src=1, nb_dst=1048576),
        ),
        ThroughputExperiment(
            "Ensō Maglev throughput (Cached)",
            iterations=iterations,
            save_name=(data_dir / Path("enso_maglev_throughput_1000_16.csv")),
            dut=EnsoMaglevDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                nb_backends=1000,
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[4],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
            pktgen_args=dict(nb_src=1, nb_dst=16),
        ),
        LatencyExperiment(
            "Ensō RTT vs. load",
            iterations=1,
            base_save_name=data_dir / Path("enso_hist.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1],
            queues_per_core=[2],
            cpu_clocks=[max_clock],
            throughput_loads=throughput_loads,
            always_save=True,
        ),
        LatencyExperiment(
            "Ensō (prefetching) RTT vs. load",
            iterations=1,
            base_save_name=data_dir / Path("enso_prefetch_hist.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
                cmd=config["paths"]["dut_enso_echo_prefetch_cmd"],
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1],
            queues_per_core=[2],
            cpu_clocks=[max_clock],
            throughput_loads=throughput_loads,
            always_save=True,
        ),
        LatencyExperiment(
            "Ensō (notification per packet) RTT vs. load",
            iterations=1,
            base_save_name=(data_dir / Path("enso_notif_per_pkt_hist.csv")),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                notif_per_pkt=True,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1],
            queues_per_core=[2],
            cpu_clocks=[max_clock],
            throughput_loads=throughput_loads,
            always_save=True,
        ),
        ThroughputExperiment(
            "Ensō throughput vs. cores",
            iterations=iterations,
            save_name=data_dir / Path("enso_throughput.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[2],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
        ),
        ThroughputExperiment(
            "Ensō throughput vs. packet size",
            iterations=iterations,
            save_name=data_dir / Path("enso_throughput.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=pkt_sizes,
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[2],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
        ),
        ThroughputExperiment(
            "Ensō throughput vs. ensō pipes (1 core)",
            iterations=iterations,
            save_name=data_dir / Path("enso_throughput.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1],
            queues_per_core=[1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
        ),
        ThroughputExperiment(
            "Ensō throughput vs. ensō pipes (2 cores)",
            iterations=iterations,
            save_name=data_dir / Path("enso_throughput.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[2],
            queues_per_core=[1, 2, 4, 8, 16, 32, 64, 128, 256, 512],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
        ),
        ThroughputExperiment(
            "Ensō throughput vs. ensō pipes (4 cores)",
            iterations=iterations,
            save_name=data_dir / Path("enso_throughput.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[4],
            queues_per_core=[1, 2, 4, 8, 16, 32, 64, 128, 256],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
        ),
        ThroughputExperiment(
            "Ensō throughput vs. ensō pipes (8 cores)",
            iterations=iterations,
            save_name=data_dir / Path("enso_throughput.csv"),
            dut=EnsoEchoDut(
                dut_nic,
                config["devices"]["enso_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[8],
            queues_per_core=[1, 2, 4, 8, 16, 32, 64, 128],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
        ),
    ]

    return experiments


async def dpdk_experiments(
    data_dir: Path,
    load_bitstream: bool,
    dpdk_type: str,
    iterations: int,
    dut_log_file: TextIO,
    pktgen_log_file: TextIO,
    config: dict[str, Any],
) -> list[Experiment]:
    skip_config = not load_bitstream

    run_setup_task = asyncio.create_task(run_setup(dut_log_file, config))

    load_pktgen_task = asyncio.create_task(
        load_pktgen(
            pktgen_log_file,
            load_bitstream,
            skip_config,
            config["devices"]["dpdk_pktgen_fpga_id"],
            config["devices"]["dpdk_pktgen_pcie"],
            config,
        )
    )

    await run_setup_task
    pktgen = await load_pktgen_task

    # We use a fixed large number of flows to make good use of RSS. This is
    # generous to DPDK since it ensures good distribution among queues with
    # RSS.
    nb_dst = 1024

    experiments: list[Experiment] = [
        ThroughputExperiment(
            "DPDK Maglev throughput (SYN flood)",
            iterations=iterations,
            save_name=(
                data_dir
                / Path(f"dpdk_{dpdk_type}_maglev_throughput_1000_1048576.csv")
            ),
            dut=DpdkMaglevDut(
                config["hosts"]["dut"],
                config["devices"]["dpdk_dut_pcie"],
                nb_backends=1000,
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[1],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
            pktgen_args=dict(nb_src=1, nb_dst=1048576),
        ),
        ThroughputExperiment(
            "DPDK Maglev throughput (Cached)",
            iterations=iterations,
            save_name=(
                data_dir
                / Path(f"dpdk_{dpdk_type}_maglev_throughput_1000_16.csv")
            ),
            dut=DpdkMaglevDut(
                config["hosts"]["dut"],
                config["devices"]["dpdk_dut_pcie"],
                nb_backends=1000,
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=[64],
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[1],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
            pktgen_args=dict(nb_src=1, nb_dst=16),
        ),
        LatencyExperiment(
            "DPDK RTT vs. load",
            iterations=1,
            base_save_name=data_dir / Path(f"dpdk_{dpdk_type}_hist.csv"),
            dut=DpdkEchoDut(
                config["hosts"]["dut"],
                config["devices"]["dpdk_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=pkt_sizes,
            nb_cores=[1],
            queues_per_core=[1],
            cpu_clocks=[max_clock],
            throughput_loads=throughput_loads,
            always_save=True,
            pktgen_args=dict(nb_src=1, nb_dst=nb_dst),
        ),
        ThroughputExperiment(
            "DPDK throughput vs. cores",
            iterations=iterations,
            save_name=data_dir / Path(f"dpdk_{dpdk_type}_throughput.csv"),
            dut=DpdkEchoDut(
                config["hosts"]["dut"],
                config["devices"]["dpdk_dut_pcie"],
                config=config,
                log_file=dut_log_file,
            ),
            pktgen=pktgen,
            pkt_sizes=pkt_sizes,
            nb_cores=[1, 2, 4, 8],
            queues_per_core=[1],
            cpu_clocks=[max_clock],
            nb_cycles=[0],
            ddio_ways=[config["extra"]["default_nb_ddio_ways"]],
            precision=100_000_000,
            pktgen_args=dict(nb_src=1, nb_dst=nb_dst),
        ),
    ]

    return experiments


async def set_remote_repos(
    dut_log_file: TextIO,
    pktgen_log_file: TextIO,
    config: dict[str, Any],
):
    with console.status("Setting up remote repos..."):
        hostname_paths = {
            config["hosts"]["dut"]: config["paths"]["dut_path"],
            config["hosts"]["pktgen"]: config["paths"]["pktgen_path"],
        }
        hostname_logs: dict[str, TextIO] = {
            config["hosts"]["dut"]: dut_log_file,
            config["hosts"]["pktgen"]: pktgen_log_file,
        }
        await update_remote_repos(hostname_paths, hostname_logs)
        await setup_remote_repos(hostname_paths, hostname_logs)


@click.command()
@click.argument("data_dir")
@click.option(
    "--load-bitstream/--no-load-bitstream",
    default=True,
    show_default=True,
    help="Enable/Disable FPGA bitstream reload.",
)
@click.option(
    "--dpdk",
    type=click.Choice(["e810"], case_sensitive=False),
    help="Run experiments for DPDK instead of Ensō.",
)
@click.option(
    "--filter",
    "-f",
    multiple=True,
    help="Filter experiments by name. Multiple filters are treated as OR.",
)
@click.option(
    "--iters",
    "-i",
    type=int,
    default=1,
    show_default=True,
    help="Number of iterations to run each experiment.",
)
@click.option(
    "--config-file",
    "-c",
    type=click.Path(exists=True),
    default="experiment_config.toml",
    show_default=True,
    help="Path to config file.",
)
@click.option(
    "--sync/--no-sync",
    default=True,
    show_default=True,
    help="Enable/Disable remote repos sync.",
)
@click.option(
    "--setup-only",
    is_flag=True,
    help="Only run setup but don't run experiments.",
    default=False,
    show_default=True,
)
def main(
    data_dir,
    load_bitstream,
    dpdk,
    filter,
    iters,
    config_file,
    sync,
    setup_only,
):
    data_dir = Path(data_dir)

    data_dir.mkdir(parents=True, exist_ok=True)

    config = set_constants(config_file)

    dut_log_file = open(config["logs"]["dut_log"], "a")
    pktgen_log_file = open(config["logs"]["pktgen_log"], "a")

    if sync:
        asyncio.run(set_remote_repos(dut_log_file, pktgen_log_file, config))
        console.log("[green]Done setting up remote repos")
    else:
        console.log("[orange1]Skipping remote repos sync")

    if setup_only:
        return

    if dpdk is not None:
        experiments = asyncio.run(
            dpdk_experiments(
                data_dir,
                load_bitstream,
                dpdk,
                iters,
                dut_log_file,
                pktgen_log_file,
                config,
            )
        )
    else:
        experiments = asyncio.run(
            enso_experiments(
                data_dir,
                load_bitstream,
                iters,
                dut_log_file,
                pktgen_log_file,
                config,
            )
        )

    exp_tracker = ExperimentTracker()

    for exp in experiments:
        if filter:
            for f in filter:
                if f in exp.name:
                    break
            else:
                continue
        exp_tracker.add_experiment(exp)

    exp_tracker.run_experiments()


if __name__ == "__main__":
    main()
