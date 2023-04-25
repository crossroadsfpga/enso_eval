#!/usr/bin/env python3

import csv
import math
import statistics
import sys

from collections import defaultdict
from itertools import cycle
from pathlib import Path
from typing import Callable
from warnings import warn

import click

import matplotlib as mpl
import matplotlib.pyplot as plt

import numpy as np

from pubplot import Document
from pubplot.document_classes import usenix


if sys.version_info < (3, 9, 0):
    raise RuntimeError("Python 3.9 or a more recent version is required.")

SYSTEM_NAME = "Ensō"
SYSTEM_NAME_SHORT = "Ensō"
BUFFER_NAME_PLURAL = "Ensō Pipes"
E810_NAME = "E810"

FILE_SUFFIX = "enso"

inches_per_pt = 1.0 / 72.27
golden_ratio = (1.0 + math.sqrt(5.0)) / 2.0
doc = Document(usenix)
width = doc.columnwidth
height = width / golden_ratio
width = width * inches_per_pt
height = height * inches_per_pt * 0.8
figsize = [width, height]

width_third = doc.textwidth / 3
height_third = width_third / golden_ratio
width_third = width_third * inches_per_pt
height_third = height_third * inches_per_pt
figsize_third = [width_third, height_third]

width_full = doc.textwidth
height_full = width_full / golden_ratio / 3
width_full = width_full * inches_per_pt
height_full = height_full * inches_per_pt
figsize_full = [width_full, height_full]

tight_layout_pad = 0.21
linewidth = 2
elinewidth = 0.5
capsize = 1
capthick = 0.5

hatch_list = ["////////", "-----", "+++++++", "|||||||"]

palette = ["#2ca02c", "dodgerblue", "#ed7d31", "#a5a5a5"]

# This is "colorBlindness::PairedColor12Steps" from R.
# Check others here: https://r-charts.com/color-palettes/#discrete
palette = [
    "#19B2FF",
    "#2ca02c",  # "#32FF00",
    "#FF7F00",
    # "#FFFF32",
    "#654CFF",
    "#E51932",
    "#FFBF7F",
    "#FFFF99",
    "#B2FF8C",
    "#A5EDFF",
    "#CCBFFF"
    # "#FF99BF"
]


linestyle = [
    (0, (1, 0)),
    (0, (4, 1)),
    (0, (2, 0.5)),
    (0, (1, 0.5)),
    (0, (0.5, 0.5)),
    (0, (4, 0.5, 0.5, 0.5)),
    (0, (3, 1, 1, 1)),
    (0, (8, 1)),
    (0, (3, 1, 1, 1, 1, 1)),
    (0, (3, 1, 1, 1, 1, 1, 1, 1)),
]

# prop_cycle = mpl.cycler(color=palette)
prop_cycle = mpl.cycler(color=palette) + mpl.cycler(linestyle=linestyle)
bar_fill = False

talk = False  # Set to true to generate talk plots.

style = {
    # Line styles.
    "axes.prop_cycle": prop_cycle,
    # Grid.
    "grid.linewidth": 0.2,
    "grid.alpha": 0.4,
    "axes.grid": True,
    "axes.axisbelow": True,
    "axes.linewidth": 0.2,
    # Ticks.
    "xtick.major.width": 0.2,
    "ytick.major.width": 0.2,
    "xtick.minor.width": 0.2,
    "ytick.minor.width": 0.2,
    # Font.
    # 'font.family': 'sans-serif',
    # 'font.family': 'Times New Roman',
    "font.family": "serif",
    "font.size": doc.footnotesize,
    "axes.labelsize": doc.footnotesize,
    "legend.fontsize": doc.scriptsize,
    "xtick.labelsize": doc.footnotesize,
    "ytick.labelsize": doc.footnotesize,
    "patch.linewidth": 0.2,
    "figure.dpi": 1000,
    "text.usetex": True,
    "text.latex.preamble": """
        \\usepackage[tt=false,type1=true]{libertine}
        \\usepackage[varqu]{zi4}
        \\usepackage[libertine]{newtxmath}
    """,
}

if talk:
    mpl.use("pgf")

    E810_NAME = "E810 with DPDK"

    style.update(
        {
            "font.family": "sans-serif",
            "font.size": 7.0,
            "axes.labelsize": 7.0,
            "legend.fontsize": 7.0,
            "xtick.labelsize": 7.0,
            "ytick.labelsize": 7.0,
            "pgf.rcfonts": False,
            "pgf.texsystem": "xelatex",
            "pgf.preamble": """
            \\usepackage{fontspec}
            \\setsansfont{Montserrat}
        """,
        }
    )

    hatch_list = [
        "///////////////////",
        "--------------",
        "++++++++++",
        "|||||||||||",
    ]


# Apply style globally.
for k, v in style.items():
    mpl.rcParams[k] = v


def bar_subplot(
    ax,
    xlabel,
    ylabel,
    xtick_labels,
    data,
    sec_y_scal_factor=None,
    sec_y_label=None,
    width_scale=0.7,
):
    x = np.arange(len(xtick_labels))  # The xtick_labels locations.
    nb_catgs = len(data)
    bar_width = width_scale / nb_catgs  # The width of the bars.
    offset = bar_width * (1 - nb_catgs) / 2

    # for d in data:
    for d, color, hatch in zip(data, cycle(palette), cycle(hatch_list)):
        yerr = d.get("errors")

        ax.bar(
            x + offset,
            d["medians"],
            bar_width,
            yerr=yerr,
            label=d["label"],
            fill=bar_fill,
            hatch=hatch,
            edgecolor=color,
            error_kw=dict(
                elinewidth=elinewidth, capsize=capsize, capthick=capthick
            ),
        )
        offset += bar_width

    if xlabel is not None:
        ax.set_xlabel(xlabel)
    if ylabel is not None:
        ax.set_ylabel(ylabel)
    ax.set_xticks(x)
    ax.set_xticklabels(xtick_labels)

    if sec_y_scal_factor is not None:
        ax2 = ax.twinx()
        min_lim, max_lim = ax.get_ylim()
        ax2.set_ylim(min_lim * sec_y_scal_factor, max_lim * sec_y_scal_factor)
        if sec_y_label is not None:
            ax2.set_ylabel(sec_y_label)
        ax.tick_params("x", length=0)
        ax2.tick_params("x", length=0)
        ax.grid(visible=False)
        ax2.grid(visible=False)
    else:
        ax.tick_params(axis="both", length=0)
        ax.grid(visible=False, axis="x")


def line_subplot(
    ax,
    xlabel,
    ylabel,
    xtick_labels,
    data,
    sec_y_scal_factor=None,
    sec_y_label=None,
    width_scale=0.7,
):
    x = np.arange(len(xtick_labels))  # The xtick_labels locations.
    nb_catgs = len(data)
    bar_width = width_scale / nb_catgs  # The width of the bars.
    offset = bar_width * (1 - nb_catgs) / 2

    # for d in data:
    for d, color, hatch in zip(data, cycle(palette), cycle(hatch_list)):
        ax.bar(
            x + offset,
            d["medians"],
            bar_width,
            yerr=d["errors"],
            label=d["label"],
            fill=bar_fill,
            hatch=hatch,
            edgecolor=color,
            error_kw=dict(
                elinewidth=elinewidth, capsize=capsize, capthick=capthick
            ),
        )
        offset += bar_width

    if xlabel is not None:
        ax.set_xlabel(xlabel)
    if ylabel is not None:
        ax.set_ylabel(ylabel)
    ax.set_xticks(x)
    ax.set_xticklabels(xtick_labels)

    if sec_y_scal_factor is not None:
        ax2 = ax.twinx()
        min_lim, max_lim = ax.get_ylim()
        ax2.set_ylim(min_lim * sec_y_scal_factor, max_lim * sec_y_scal_factor)
        if sec_y_label is not None:
            ax2.set_ylabel(sec_y_label)
        ax.tick_params("x", length=0)
        ax2.tick_params("x", length=0)
        ax.grid(visible=False)
        ax2.grid(visible=False)
    else:
        ax.tick_params(axis="both", length=0)
        ax.grid(visible=False, axis="x")


def bar_plot(
    xlabel,
    ylabel,
    xtick_labels,
    data,
    fig_name,
    dest_dir,
    opts,
    set_figsize=None,
    legend_kwargs=None,
    hide_legend=False,
    sec_y_scal_factor=None,
    sec_y_label=None,
    on_end=None,
    width_scale=0.7,
):
    if len(data) == 0:
        return

    fig, ax = plt.subplots()

    bar_subplot(
        ax,
        xlabel,
        ylabel,
        xtick_labels,
        data,
        sec_y_scal_factor,
        sec_y_label,
        width_scale,
    )

    if set_figsize is None:
        set_figsize = figsize

    if on_end is not None:
        on_end(fig, ax)

    if not hide_legend:
        if legend_kwargs is None:
            ax.legend()
        else:
            ax.legend(**legend_kwargs)

    fig.set_size_inches(*set_figsize)
    fig.tight_layout(pad=0.1)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def filter_row(row: dict[str, str], filter: dict[str, str]) -> bool:
    for k, v in filter.items():
        if row[k] != v:
            return True
    return False


def __generic_plot_rate_vs_cores(
    data_dir: Path,
    dest_dir: Path,
    configs: dict[str, tuple[str, Path]],
    data_filter: dict[str, dict[str, str]],
    fig_name: str,
    legend_kwargs: dict,
    opts: dict,
    use_rates: bool = False,
    use_throughput=True,
    set_figsize=None,
    show_eth_line_on_legend=True,
    **kwargs,
) -> None:
    nb_cores_list = [1, 2, 4, 8]
    data = []
    for config_name, (label, config_file) in configs.items():
        rate_by_nb_cores = defaultdict(list)
        throughput_by_nb_cores = defaultdict(list)

        # If file does not exist, skip it.
        if not (data_dir / config_file).exists():
            continue

        with open(data_dir / config_file, newline="") as f:
            rd = csv.DictReader(f)
            for row in rd:
                if filter_row(row, data_filter[config_name]):
                    continue

                nb_cores = int(row["nb_cores"])
                pkt_size = int(row["pkt_size"])
                if use_rates:
                    assert pkt_size == 64
                throughput = float(row["throughput"])
                packet_rate_mpps = (throughput / ((pkt_size + 20) * 8)) / 1e6
                rate_by_nb_cores[nb_cores].append(packet_rate_mpps)
                throughput_by_nb_cores[nb_cores].append(throughput * 1e-9)

        rate_medians = []
        rate_errors = []
        for nb_cores in nb_cores_list:
            if use_rates:
                rates = rate_by_nb_cores[nb_cores]
            else:
                rates = throughput_by_nb_cores[nb_cores]

            if len(rates) == 0:
                continue

            rates.sort()

            median = statistics.median(rates)
            if len(rates) < 2:
                stddev = 0
                warn("Not enough samples to calculate stddev")
            else:
                stddev = statistics.stdev(rates)
            rate_medians.append(median)
            rate_errors.append(stddev)

        if len(rate_medians) == 0:
            continue

        config_summary = {
            "medians": rate_medians,
            "errors": rate_errors,
            "label": label,
        }

        data.append(config_summary)

    if len(data) == 0:
        return

    sec_y_scal_factor = ((64 + 20) * 8) / 1e3

    def add_eth_line(fig, ax):
        if show_eth_line_on_legend:
            label = "100\\,Gb Eth."
        else:
            label = None
        ax.axhline(y=148.8, color="r", linestyle="--", lw=0.5, label=label)

    if use_rates and use_throughput:
        y_label = "Packet rate (Mpps)"

        if "on_end" in kwargs:

            def on_end(fig, ax):
                kwargs["on_end"](fig, ax)
                add_eth_line(fig, ax)

        else:
            on_end = add_eth_line

        kwargs.update(
            {
                "sec_y_scal_factor": sec_y_scal_factor,
                "sec_y_label": "Throughput (Gbps)",
                "on_end": on_end,
            }
        )
    elif use_throughput:
        y_label = "Throughput (Gbps)"
    elif use_rates:
        y_label = "Packet rate (Mpps)"

        if "on_end" in kwargs:
            func = kwargs["on_end"]

            def on_end(fig, ax):
                func(fig, ax)
                add_eth_line(fig, ax)

        else:
            on_end = add_eth_line

        kwargs.update({"on_end": on_end})
    else:
        raise RuntimeError("Must choose throughput or rate")

    xtick_labels = nb_cores_list
    bar_plot(
        "Number of cores",
        y_label,
        xtick_labels,
        data,
        fig_name,
        dest_dir,
        opts,
        set_figsize=set_figsize,
        legend_kwargs=legend_kwargs,
        **kwargs,
    )


def __generic_plot_rate_vs_ddio_ways(
    data_dir: Path,
    dest_dir: Path,
    configs: dict[str, tuple[str, Path]],
    data_filter: dict[str, dict[str, str]],
    fig_name: str,
    legend_kwargs: dict,
    opts: dict,
    use_rates: bool = False,
    use_throughput=True,
    set_figsize=None,
    show_eth_line_on_legend=True,
) -> None:
    ddio_ways_list = [0, 1, 2, 4, 8, 11]
    fig, ax = plt.subplots()

    for config_name, (label, config_file) in configs.items():
        rate_by_ddio_ways = defaultdict(list)
        throughput_by_ddio_ways = defaultdict(list)
        with open(data_dir / config_file, newline="") as f:
            rd = csv.DictReader(f)
            for row in rd:
                if filter_row(row, data_filter[config_name]):
                    continue

                ddio_ways = int(row["ddio_ways"])
                pkt_size = int(row["pkt_size"])
                if use_rates:
                    assert pkt_size == 64
                throughput = float(row["throughput"])
                packet_rate_mpps = (throughput / ((pkt_size + 20) * 8)) / 1e6
                rate_by_ddio_ways[ddio_ways].append(packet_rate_mpps)
                throughput_by_ddio_ways[ddio_ways].append(throughput * 1e-9)

        rate_medians = []
        rate_errors = []

        for ddio_ways in ddio_ways_list:
            if use_rates:
                rates = rate_by_ddio_ways[ddio_ways]
            else:
                rates = throughput_by_ddio_ways[ddio_ways]

            rates.sort()

            median = statistics.median(rates)
            if len(rates) < 2:
                stddev = 0
                warn("Not enough samples to calculate stddev")
            else:
                stddev = statistics.stdev(rates)
            rate_medians.append(median)
            rate_errors.append(stddev)

        ax.errorbar(
            ddio_ways_list, rate_medians, yerr=rate_errors, label=label
        )

    if use_rates and use_throughput:
        y_label = "Packet rate (Mpps)"
    elif use_throughput:
        y_label = "Throughput (Gbps)"
        ax.set_ylim(0, 100)
    elif use_rates:
        y_label = "Packet rate (Mpps)"
        ax.set_ylim(0, 150)
    else:
        raise RuntimeError("Must choose throughput or rate")

    ax.set_xlabel("Number of DDIO ways")
    ax.set_ylabel(y_label)

    if legend_kwargs is None:
        ax.legend()
    else:
        ax.legend(**legend_kwargs)

    if set_figsize is None:
        set_figsize = figsize

    fig.set_size_inches(*set_figsize)
    fig.tight_layout(pad=0.1)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_rate_vs_cores(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    configs = {
        "enso": (SYSTEM_NAME, f"{FILE_SUFFIX}_throughput.csv"),
        "e810": (E810_NAME, "dpdk_e810_throughput.csv"),
    }

    data_filter = {
        "enso": {
            "pkt_size": "64",
            "queues_per_core": "2",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
        },
        "e810": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
        },
    }

    # set_fig_size = [width_third, height_third * 0.8]
    set_fig_size = [width, height]

    def set_ticks(_, ax):
        ax.axhline(y=148.8, color="r", linestyle="--", lw=0.5, label=None)
        ax.set_yticks([0, 50, 100, 150])

    show_eth_line_on_legend = talk

    __generic_plot_rate_vs_cores(
        data_dir,
        dest_dir,
        configs,
        data_filter,
        "rate_vs_cores",
        set_figsize=set_fig_size,
        legend_kwargs={"loc": "lower right", "ncol": 3},
        opts=opts,
        use_rates=True,
        use_throughput=False,
        show_eth_line_on_legend=show_eth_line_on_legend,
        on_end=set_ticks,
    )


def plot_rate_vs_cores_vs_pkt_sizes(
    data_dir: Path, dest_dir: Path, opts: dict
) -> None:
    pkt_sizes = [64, 128, 256, 512, 1024, 1518]
    nb_cores_list = [1, 2, 4, 8]
    fig_name = "rate_vs_cores_vs_pkt_sizes"

    base_configs = {
        "enso": (SYSTEM_NAME, f"{FILE_SUFFIX}_throughput.csv"),
        "e810": (E810_NAME, "dpdk_e810_throughput.csv"),
    }

    base_data_filter = {
        "enso": {
            "queues_per_core": "2",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
        },
        "e810": {
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
        },
    }

    fig, axes = plt.subplots(nrows=1, ncols=len(nb_cores_list), sharey=True)

    for nb_cores, ax in zip(nb_cores_list, axes):
        data = []

        for config_name, (label, config_file) in base_configs.items():
            rate_by_pkt_size = defaultdict(list)
            throughput_by_pkt_size = defaultdict(list)

            data_filter = base_data_filter[config_name].copy()
            data_filter["nb_cores"] = str(nb_cores)
            config_file_path = data_dir / config_file

            if not config_file_path.exists():
                continue

            with open(config_file_path, newline="") as f:
                rd = csv.DictReader(f)
                for row in rd:
                    if filter_row(row, data_filter):
                        continue

                    pkt_size = int(row["pkt_size"])
                    throughput = float(row["throughput"])
                    packet_rate = throughput / ((pkt_size + 20) * 8)
                    packet_rate_mpps = packet_rate / 1e6
                    rate_by_pkt_size[pkt_size].append(packet_rate_mpps)
                    throughput_by_pkt_size[pkt_size].append(throughput * 1e-9)

            rate_medians = []
            rate_errors = []

            valid_data = False
            for pkt_size in pkt_sizes:
                rates = throughput_by_pkt_size[pkt_size]

                if len(rates) == 0:
                    rate_medians.append(0)
                    rate_errors.append(0)
                    continue

                valid_data = True

                rates.sort()

                median = statistics.median(rates)
                if len(rates) < 2:
                    stddev = 0
                    warn("Not enough samples to calculate stddev")
                else:
                    stddev = statistics.stdev(rates)
                rate_medians.append(median)
                rate_errors.append(stddev)

            if not valid_data:
                continue

            config_summary = {
                "medians": rate_medians,
                "errors": rate_errors,
                "label": label,
            }
            data.append(config_summary)

        if len(data) == 0:
            return

        if nb_cores == 1:
            ylabel = "Throughput (Gbps)"
            title = "1 core"
        else:
            ylabel = None
            title = f"{nb_cores} cores"

        bar_subplot(ax, "Packet size (bytes)", ylabel, pkt_sizes, data)

        ax.annotate(
            title,
            xy=(0.02, 0.8),
            xycoords="axes fraction",
            horizontalalignment="left",
            verticalalignment="bottom",
        )

        ax.set_ylim(0, 130)
        ax.set_yticks([0, 25, 50, 75, 100])

        for tick in ax.get_xticklabels():
            tick.set_rotation(45)
            tick.set_rotation_mode("anchor")
            tick.set_ha("right")
        ax.set_yticks([0, 25, 50, 75, 100])

    set_figsize = [width_full * 0.65, height_third * 0.8]

    fig.set_size_inches(*set_figsize)

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(
        handles,
        labels,
        loc="lower right",
        ncol=2,
        bbox_to_anchor=(0, 0.825, 1, 1),
    )

    # We need to leave space for the legend on top.
    fig.tight_layout(rect=(0, 0, 1, 0.84), pad=0.0)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_maglev(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    fig_name = "maglev"

    configs = {
        "enso_cached": (
            f"{SYSTEM_NAME} (Cached)",
            f"{FILE_SUFFIX}_maglev_throughput_1000_16.csv",
        ),
        "enso_syn": (
            f"{SYSTEM_NAME} (SYN Flood)",
            f"{FILE_SUFFIX}_maglev_throughput_1000_1048576.csv",
        ),
        "e810_cached": (
            "E810 (Cached)",
            "dpdk_e810_maglev_throughput_1000_16.csv",
        ),
        "e810_syn": (
            "E810 (SYN Flood)",
            "dpdk_e810_maglev_throughput_1000_1048576.csv",
        ),
    }

    data_filter = {
        "enso_cached": {
            "pkt_size": "64",
            "queues_per_core": "4",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "ddio_ways": "2",
        },
        "enso_syn": {
            "pkt_size": "64",
            "queues_per_core": "4",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "ddio_ways": "2",
        },
        "e810_cached": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "ddio_ways": "2",
        },
        "e810_syn": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "ddio_ways": "2",
        },
    }

    set_figsize = [width_third, height_third]

    def set_ticks(_, ax):
        ax.axhline(y=148.8, color="r", linestyle="--", lw=0.5, label=None)
        ax.set_yticks([0, 50, 100, 150])

    __generic_plot_rate_vs_cores(
        data_dir,
        dest_dir,
        configs,
        data_filter,
        fig_name,
        use_rates=True,
        use_throughput=False,
        legend_kwargs=dict(
            loc="lower right", ncol=2, bbox_to_anchor=(0, 1, 1, 1)
        ),
        opts=opts,
        show_eth_line_on_legend=False,
        set_figsize=set_figsize,
        on_end=set_ticks,
    )


def plot_maglev_ddio(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    enso_configs = {
        "enso_syn_1": (
            f"{SYSTEM_NAME} (1 core)",
            Path(f"{FILE_SUFFIX}_maglev_throughput_1000_1048576.csv"),
        ),
        "enso_syn_2": (
            f"{SYSTEM_NAME} (2 cores)",
            Path(f"{FILE_SUFFIX}_maglev_throughput_1000_1048576.csv"),
        ),
        "enso_syn_4": (
            f"{SYSTEM_NAME} (4 cores)",
            Path(f"{FILE_SUFFIX}_maglev_throughput_1000_1048576.csv"),
        ),
        "enso_syn_8": (
            f"{SYSTEM_NAME} (8 cores)",
            Path(f"{FILE_SUFFIX}_maglev_throughput_1000_1048576.csv"),
        ),
    }
    e810_configs = {
        "e810_syn_1": (
            "E810 (1 core)",
            Path("dpdk_e810_maglev_throughput_1000_1048576.csv"),
        ),
        "e810_syn_2": (
            "E810 (2 cores)",
            Path("dpdk_e810_maglev_throughput_1000_1048576.csv"),
        ),
        "e810_syn_4": (
            "E810 (4 cores)",
            Path("dpdk_e810_maglev_throughput_1000_1048576.csv"),
        ),
        "e810_syn_8": (
            "E810 (8 cores)",
            Path("dpdk_e810_maglev_throughput_1000_1048576.csv"),
        ),
    }

    data_filter = {
        "enso_syn_1": {
            "pkt_size": "64",
            "queues_per_core": "4",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "1",
        },
        "enso_syn_2": {
            "pkt_size": "64",
            "queues_per_core": "4",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "2",
        },
        "enso_syn_4": {
            "pkt_size": "64",
            "queues_per_core": "4",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "4",
        },
        "enso_syn_8": {
            "pkt_size": "64",
            "queues_per_core": "4",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "8",
        },
        "e810_syn_1": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "1",
        },
        "e810_syn_2": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "2",
        },
        "e810_syn_4": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "4",
        },
        "e810_syn_8": {
            "pkt_size": "64",
            "queues_per_core": "1",
            "cpu_clock": "3100000",
            "nb_cycles": "0",
            "nb_cores": "8",
        },
    }
    __generic_plot_rate_vs_ddio_ways(
        data_dir,
        dest_dir,
        enso_configs,
        data_filter,
        "maglev_ddio_enso",
        opts=opts,
        use_rates=True,
        use_throughput=False,
        legend_kwargs=dict(
            loc="lower right", ncol=2, bbox_to_anchor=(0, 1, 1, 1)
        ),
        show_eth_line_on_legend=False,
    )

    __generic_plot_rate_vs_ddio_ways(
        data_dir,
        dest_dir,
        e810_configs,
        data_filter,
        "maglev_ddio_e810",
        opts=opts,
        use_rates=True,
        use_throughput=False,
        legend_kwargs=dict(
            loc="lower right", ncol=2, bbox_to_anchor=(0, 1, 1, 1)
        ),
        show_eth_line_on_legend=False,
    )


def _generic_subplot_rtt_vs_load(
    ax,
    data_dir: Path,
    configs: dict[str, tuple[str, str, str]],
    data_filter: dict[str, dict[str, str]],
    plot_p50=False,
    plot_p99=True,
    plot_p99_9=False,
    verbose=False,
) -> bool:
    # In Gbps.
    loads = [1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 99, 100]

    # Convert to bps.
    loads = [load * 1_000_000_000 for load in loads]

    valid_data = False  # Whether we have any data to plot.

    for config_name, (label, base_file, csv_file_pattern) in configs.items():
        p50s = []
        p99s = []
        p99_9s = []
        means = []
        throughputs = []

        throughput_by_load = defaultdict(list)
        base_file_path = data_dir / base_file

        if not base_file_path.exists():
            continue

        with open(base_file_path, newline="") as f:
            rd = csv.DictReader(f)
            for row in rd:
                if filter_row(row, data_filter[config_name]):
                    continue

                load = int(row["load"])
                throughput_gbps = float(row["throughput"]) / 1e9
                throughput_by_load[load].append(throughput_gbps)

        for load in loads:
            csv_file = csv_file_pattern.replace("{load}", str(load))
            file_path = data_dir / csv_file
            if not file_path.exists():
                continue

            throughput = statistics.median(throughput_by_load[load])
            throughputs.append(throughput)
            data = np.loadtxt(data_dir / csv_file, delimiter=",")
            x = data[:, 0]  # Bins
            y = data[:, 1]  # Number of elements in each bin.

            # Sort by bins.
            order = x.argsort()
            x = x[order]
            y = y[order]

            # Normalize counters.
            y /= y.sum()

            mean = np.sum(x * y) / 1e3
            means.append(mean)

            cumsum_y = np.cumsum(y)

            # Convert from ns to us.
            x /= 1e3

            p50 = x[np.searchsorted(cumsum_y, 0.5)]
            p99 = x[np.searchsorted(cumsum_y, 0.99)]
            p99_9 = x[np.searchsorted(cumsum_y, 0.999)]

            p50s.append(p50)
            p99s.append(p99)
            p99_9s.append(p99_9)

        if verbose:
            print(label)
            for tpt, mean, p50, p99 in zip(throughputs, means, p50s, p99s):
                print("tpt:", tpt, "mean:", mean, "p50:", p50, "p99:", p99)
            print("")

        # Filter out zero throughput (that means that there were packet drops).
        means = [m for m, t in zip(means, throughputs) if t > 0]
        p50s = [p for p, t in zip(p50s, throughputs) if t > 0]
        p99s = [p for p, t in zip(p99s, throughputs) if t > 0]
        p99_9s = [p for p, t in zip(p99_9s, throughputs) if t > 0]
        throughputs = [t for t in throughputs if t > 0]

        if len(throughputs) == 0:
            continue

        if plot_p99_9:
            valid_data = True
            ax.plot(
                throughputs,
                p99_9s,
                linewidth=linewidth,
                label=f"{label} 99.9\\textsuperscript{{th}} pctl.",
            )
        if plot_p99:
            valid_data = True
            ax.plot(
                throughputs,
                p99s,
                linewidth=linewidth,
                label=f"{label} 99\\textsuperscript{{th}} pctl.",
            )
        if plot_p50:
            valid_data = True
            ax.plot(
                throughputs,
                p50s,
                linewidth=linewidth,
                label=f"{label} 50\\textsuperscript{{th}} pctl.",
            )

    if not valid_data:
        return False

    ax.set_ylim(0, 50)
    ax.tick_params("y", length=0)
    ax.tick_params("x", length=0)

    ax.set_xlabel("Offered load (Gbps)")
    ax.set_ylabel(r"Latency ({\textmu}s)")

    return True


def plot_pcie_bw(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    pcie_bw_prefix = "pcie_bw-64_{cores}"
    configs = {
        FILE_SUFFIX: (
            SYSTEM_NAME_SHORT,
            f"{FILE_SUFFIX}_{pcie_bw_prefix}_2_3100000_100000000000.csv",
            "Socket0,IIO Stack 2 - PCIe1,Part0 (1st x16/x8/x4),",
        ),
        "dpdk_e810": (
            E810_NAME,
            f"dpdk_e810_{pcie_bw_prefix}_1_3100000_100000000000.csv",
            "Socket0,IIO Stack 1 - PCIe0,Part0 (1st x16/x8/x4),",
        ),
    }

    pcie_bw_names = {
        FILE_SUFFIX: f"{SYSTEM_NAME_SHORT} Goodput",
        "dpdk_e810": "E810 Goodput",
    }

    nb_cores_list = [1, 2, 4, 8]
    goodput_by_cores = defaultdict(dict)
    data = []

    for name, (label, config_file_pattern, line_start) in configs.items():
        dma_rd_medians = []
        dma_wr_medians = []
        dma_rd_errors = []
        dma_wr_errors = []

        file_path = data_dir / f"{name}_pcie_bw.csv"

        if not file_path.exists():
            continue

        with open(file_path, newline="") as f:
            rd = csv.DictReader(f)
            for row in rd:
                nb_cores = int(row["nb_cores"])
                pkt_size = int(row["pkt_size"])
                assert pkt_size == 64

                throughput_gbps = float(row["throughput"]) / 1e9
                goodput_gbps = throughput_gbps * 64 / 84
                goodput_by_cores[name][nb_cores] = goodput_gbps

        for nb_cores in nb_cores_list:
            config_file = config_file_pattern.replace("{cores}", str(nb_cores))
            file_path = data_dir / config_file

            if not file_path.exists():
                continue

            dma_reads = []
            dma_writes = []
            mmio_reads = []
            mmio_writes = []

            with open(file_path, "r") as f:
                lines = f.readlines()

            for line in (ln for ln in lines if ln.startswith(line_start)):
                values = line.split(line_start)[1].split(",")
                dma_wr = int(values[0])  # Called "Inbound write" by PCM.
                dma_rd = int(values[1])  # Called "Inbound read" by PCM.
                mmio_rd = int(values[2])  # Called "Outbound read" by PCM.
                mmio_wr = int(values[3])  # Called "Outbound write" by PCM.

                dma_reads.append(dma_rd * 8 / 1e9)
                dma_writes.append(dma_wr * 8 / 1e9)
                mmio_reads.append(mmio_rd)
                mmio_writes.append(mmio_wr)

            dma_reads.sort()

            dma_writes.sort()

            dma_rd_median = statistics.median(dma_reads)
            dma_wr_median = statistics.median(dma_writes)

            if len(dma_reads) < 2:
                dma_rd_stddev = 0
                dma_wr_stddev = 0
                warn("Not enough samples to calculate stddev")
            else:
                dma_rd_stddev = statistics.stdev(dma_reads)
                dma_wr_stddev = statistics.stdev(dma_writes)

            dma_rd_medians.append(dma_rd_median)
            dma_wr_medians.append(dma_wr_median)
            dma_rd_errors.append(dma_rd_stddev)
            dma_wr_errors.append(dma_wr_stddev)

        if not dma_rd_medians or not dma_wr_medians:
            continue

        config_summary = {
            "medians": dma_rd_medians,
            "errors": dma_rd_errors,
            "label": f"{label} RD",
        }
        data.append(config_summary)

        config_summary = {
            "medians": dma_wr_medians,
            "errors": dma_wr_errors,
            "label": f"{label} WR",
        }
        data.append(config_summary)

    if len(data) == 0:
        return

    xtick_labels = nb_cores_list

    fig_name = "pcie_bw"
    fig, ax = plt.subplots()

    markers = ["k^-", "kx-"]

    for mark, (label, tpt_by_cores) in zip(markers, goodput_by_cores.items()):
        y = [tpt_by_cores[c] for c in nb_cores_list]
        # print(label, y)
        ax.plot(
            [str(c) for c in nb_cores_list],
            y,
            mark,
            markersize=5,
            linewidth=0.8,
            markerfacecolor="None",
            markeredgewidth=0.8,
            label=pcie_bw_names[label],
        )

    if len(data) == 0:
        return

    bar_subplot(
        ax, "Number of cores", "PCIe bandwidth (Gbps)", xtick_labels, data
    )

    ax.legend(loc="lower right", ncol=3)

    set_fig_size = [width, height * 0.9]

    fig.set_size_inches(*set_fig_size)
    fig.tight_layout(pad=0.1)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_rtt_vs_load_reactive_notif(
    data_dir: Path, dest_dir: Path, opts: dict
) -> None:
    fig_name = "rtt_vs_load_reactive_notif"
    q_per_core = 2

    configs = {
        "reactive_notif": (
            "Reactive",
            f"{FILE_SUFFIX}_hist.csv",
            f"{FILE_SUFFIX}_hist-64_1_{q_per_core}_3100000_" + "{load}.csv",
        ),
        "notif_per_pkt": (
            "Per packet",
            f"{FILE_SUFFIX}_notif_per_pkt_hist.csv",
            f"{FILE_SUFFIX}_notif_per_pkt_hist-64_1_{q_per_core}_3100000_"
            + "{load}.csv",
        ),
    }
    data_filter = {
        "reactive_notif": {
            "pkt_size": "64",
            "queues_per_core": f"{q_per_core}",
            "nb_cores": "1",
        },
        "notif_per_pkt": {
            "pkt_size": "64",
            "queues_per_core": f"{q_per_core}",
            "nb_cores": "1",
        },
    }

    fig, ax = plt.subplots()
    fig.set_size_inches(width * 0.8, height * 0.6)

    valid_data = _generic_subplot_rtt_vs_load(
        ax, data_dir, configs, data_filter, plot_p50=True, plot_p99=True
    )

    if not valid_data:
        return

    plt.legend(bbox_to_anchor=(1, 1.05))

    ax.set_xticks([0, 25, 50, 75, 100])
    ax.set_yticks([0, 10, 20, 30, 40, 50])

    fig.tight_layout(pad=tight_layout_pad)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_rtt_vs_load_pref_notif(
    data_dir: Path, dest_dir: Path, opts: dict
) -> None:
    fig_name = "rtt_vs_load_pref_notif"

    enso_prefix = FILE_SUFFIX
    enso_lat_opt_prefix = f"{FILE_SUFFIX}_prefetch"

    q_per_core = 2

    configs = {
        "enso_64": (
            f"{SYSTEM_NAME_SHORT} (No pref.)",
            f"{enso_prefix}_hist.csv",
            f"{enso_prefix}_hist-64_1_{q_per_core}_3100000_{{load}}.csv",
        ),
        "enso_lat_opt_64": (
            f"{SYSTEM_NAME_SHORT} (Pref.)",
            f"{enso_lat_opt_prefix}_hist.csv",
            f"{enso_lat_opt_prefix}_hist-64_1_{q_per_core}_3100000_{{load}}.csv",
        ),
        "e810_64": (
            E810_NAME,
            "dpdk_e810_hist.csv",
            "dpdk_e810_hist-64_1_1_3100000_{load}.csv",
        ),
    }
    data_filter = {
        "enso_64": {
            "pkt_size": "64",
            "queues_per_core": f"{q_per_core}",
            "nb_cores": "1",
        },
        "enso_lat_opt_64": {
            "pkt_size": "64",
            "queues_per_core": f"{q_per_core}",
            "nb_cores": "1",
        },
        "e810_64": {"pkt_size": "64", "queues_per_core": "1", "nb_cores": "1"},
    }

    fig, ax = plt.subplots()
    fig.set_size_inches(width * 0.9, height * 0.7)

    valid_data = _generic_subplot_rtt_vs_load(
        ax, data_dir, configs, data_filter, plot_p50=True, plot_p99=True
    )

    if not valid_data:
        return

    plt.legend(bbox_to_anchor=(1, 1.05))

    ax.set_xticks([0, 25, 50, 75, 100])
    ax.set_yticks([0, 10, 20, 30, 40, 50])

    fig.tight_layout(pad=tight_layout_pad)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_rate_vs_nb_pipes(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    configs = {
        "single_dsc_queue_1": (
            "1 core",
            data_dir / f"{FILE_SUFFIX}_throughput.csv",
        ),
        "single_dsc_queue_2": (
            "2 cores",
            data_dir / f"{FILE_SUFFIX}_throughput.csv",
        ),
        "single_dsc_queue_4": (
            "4 cores",
            data_dir / f"{FILE_SUFFIX}_throughput.csv",
        ),
        "single_dsc_queue_8": (
            "8 cores",
            data_dir / f"{FILE_SUFFIX}_throughput.csv",
        ),
    }

    data_filter = {
        "single_dsc_queue_1": {
            "pkt_size": "64",
            "cpu_clock": "3100000",
            "nb_cores": "1",
            "nb_cycles": "0",
        },
        "single_dsc_queue_2": {
            "pkt_size": "64",
            "cpu_clock": "3100000",
            "nb_cores": "2",
            "nb_cycles": "0",
        },
        "single_dsc_queue_4": {
            "pkt_size": "64",
            "cpu_clock": "3100000",
            "nb_cores": "4",
            "nb_cycles": "0",
        },
        "single_dsc_queue_8": {
            "pkt_size": "64",
            "cpu_clock": "3100000",
            "nb_cores": "8",
            "nb_cycles": "0",
        },
    }

    nb_queues_list = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]
    data = []
    for config_name, (label, config_file) in configs.items():
        rate_by_nb_queues = defaultdict(list)

        if not config_file.exists():
            continue

        with open(config_file, newline="") as f:
            rd = csv.DictReader(f)
            for row in rd:
                dt_f = data_filter[config_name]
                if filter_row(row, dt_f):
                    continue

                nb_queues = int(row["queues_per_core"]) * int(dt_f["nb_cores"])
                pkt_size = int(row["pkt_size"])
                throughput = float(row["throughput"])
                packet_rate_mpps = (throughput / ((pkt_size + 20) * 8)) / 1e6
                rate_by_nb_queues[nb_queues].append(packet_rate_mpps)

        rate_medians = []
        rate_errors = []

        valid_data = False
        for nb_queues in nb_queues_list:
            rates = rate_by_nb_queues[nb_queues]

            if len(rates) == 0:
                rate_medians.append(0)
                rate_errors.append(0)
                continue

            valid_data = True

            rates.sort()
            median = statistics.median(rates)
            if len(rates) < 2:
                stddev = 0
                warn("Not enough samples to calculate stddev")
            else:
                stddev = statistics.stdev(rates)
            rate_medians.append(median)
            rate_errors.append(stddev)

        if not valid_data:
            continue

        config_summary = {
            "medians": rate_medians,
            "errors": rate_errors,
            "label": label,
        }
        data.append(config_summary)

    my_figsize = [width_third, height_third * 0.8]

    def set_ticks(_, ax):
        ax.axhline(y=148.8, color="r", linestyle="--", lw=0.5, label=None)
        ax.set_yticks([0, 50, 100, 150])

    xtick_labels = nb_queues_list
    bar_plot(
        f"Number of {BUFFER_NAME_PLURAL}",
        "Packet rate (Mpps)",
        xtick_labels,
        data,
        "rate_vs_nb_pipes",
        dest_dir,
        opts,
        set_figsize=my_figsize,
        legend_kwargs={"loc": "lower right", "ncol": 2},
        width_scale=0.8,
        on_end=set_ticks,
    )


def plot_mica_throughput(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    fig_name = "mica_throughput"
    config_file = "mica_throughput.csv"

    nb_cores_list = [1, 2, 4, 8]
    data = []
    dpdk_rate_by_nb_cores = {}
    enso_rate_by_nb_cores = {}

    config_file_path = data_dir / config_file

    if not config_file_path.exists():
        return

    with open(config_file_path, newline="") as f:
        rd = csv.DictReader(f)
        for row in rd:
            nb_cores = int(row["nb_cores"])
            rate_dpdk = float(row["tpt_dpdk"])
            rate_enso = float(row[f"tpt_{FILE_SUFFIX}"])
            dpdk_rate_by_nb_cores[nb_cores] = rate_dpdk
            enso_rate_by_nb_cores[nb_cores] = rate_enso

    config_summary = {
        "medians": [enso_rate_by_nb_cores[c] for c in nb_cores_list],
        "errors": [0] * len(nb_cores_list),
        "label": SYSTEM_NAME,
    }

    data.append(config_summary)

    config_summary = {
        "medians": [dpdk_rate_by_nb_cores[c] for c in nb_cores_list],
        "errors": [0] * len(nb_cores_list),
        "label": E810_NAME,
    }

    data.append(config_summary)

    y_label = "Request rate (Mops)"

    xtick_labels = nb_cores_list
    bar_plot(
        "Number of cores",
        y_label,
        xtick_labels,
        data,
        fig_name,
        dest_dir,
        opts,
        set_figsize=figsize_third,
    )


def plot_mica_latency(
    data_dir: Path, dest_dir: Path, opts: dict, lat_opt=False
):
    fig_name = "mica_latency"
    config_file = "mica_latency.csv"

    dpdk_tpt = []
    dpdk_rtts = []
    dpdk_errors = []

    enso_tpt = []
    enso_rtts = []
    enso_errors = []

    config_file_path = data_dir / config_file

    if not config_file_path.exists():
        return

    with open(config_file_path, newline="") as f:
        rd = csv.DictReader(f)
        for row in rd:
            dpdk_tpt.append(float(row["tpt_dpdk"]))
            dpdk_rtts.append(float(row["lat_dpdk"]))
            dpdk_errors.append(float(row["lat_dpdk_err"]))
            enso_tpt.append(float(row[f"tpt_{FILE_SUFFIX}"]))
            enso_rtts.append(float(row[f"lat_{FILE_SUFFIX}"]))
            enso_errors.append(float(row[f"lat_{FILE_SUFFIX}_err"]))

    fig, ax = plt.subplots()
    fig.set_size_inches(*figsize_third)
    ax.plot(
        enso_tpt,
        enso_rtts,
        label=SYSTEM_NAME,
    )

    ax.plot(
        dpdk_tpt,
        dpdk_rtts,
        label=E810_NAME,
    )

    # ax.set_ylim(0, 50)

    ax.set_xticks([0, 1, 2, 3, 4, 5, 6, 7])

    plt.legend()

    ax.set_xlabel("Offered load (Mops)")
    ax.set_ylabel(r"Latency ({\textmu}s)")

    ax.tick_params("y", length=0)
    ax.tick_params("x", length=0)

    fig.tight_layout(pad=tight_layout_pad)

    ax.text(5.5, 40, "Packet loss", color="#2ca02c", rotation=90.0)

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_log_monitor(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    file_name = "log_monitor.csv"

    targets = []
    e810_throughput = []
    enso_throughput = []

    file_path = data_dir / file_name

    if not file_path.exists():
        return

    with open(file_path, newline="") as f:
        rd = csv.DictReader(f)
        for row in rd:
            target = row["target"]
            targets.append(f"\\texttt{{{target}}}")
            e810_throughput.append(float(row["e810"]))
            enso_throughput.append(float(row[FILE_SUFFIX]))

    data = [
        {
            "label": SYSTEM_NAME,
            "medians": enso_throughput,
        },
        {
            "label": E810_NAME,
            "medians": e810_throughput,
        },
    ]

    fig_name = "log_monitor"

    def set_ticks(_, ax):
        for tick in ax.get_xticklabels():
            tick.set_rotation(45)
            tick.set_rotation_mode("anchor")
            tick.set_ha("right")
            # tick.set_fontsize(doc.tiny)
            ax.set_yticks([0, 25, 50, 75, 100])

    # set_figsize = [width, height * 0.9]
    set_figsize = figsize_third

    bar_plot(
        "Target application log",
        "Throughput (Gbps)",
        targets,
        data,
        fig_name,
        dest_dir,
        opts,
        set_figsize=set_figsize,
        on_end=set_ticks,
    )


def plot_motivation_pcie_bw(
    data_dir: Path, dest_dir: Path, opts: dict
) -> None:
    fig, ax = plt.subplots()

    e810_2_cores_goodput_gbps = 51.389896512315275
    e810_2_cores_pcie_rd_bw = 84.15157618
    e810_2_cores_pcie_wr_bw = 74.46466083199999

    labels = ["RD", "WR"]

    goodput = [e810_2_cores_goodput_gbps, e810_2_cores_goodput_gbps]
    metadata = [
        e810_2_cores_pcie_rd_bw - e810_2_cores_goodput_gbps,
        e810_2_cores_pcie_wr_bw - e810_2_cores_goodput_gbps,
    ]

    ax.bar(
        labels,
        goodput,
        label="Goodput",
        fill=bar_fill,
        hatch=hatch_list[1],
        edgecolor=palette[1],
    )
    ax.bar(
        labels,
        metadata,
        bottom=goodput,
        label="Metadata",
        fill=bar_fill,
        hatch=hatch_list[0],
        edgecolor=palette[0],
    )

    ax.axhline(y=85, color="r", linestyle="--", lw=0.5, label="PCIe limit")

    ax.set_ylabel("PCIe BW (Gbps)")

    ax.set_yticks([0, 20, 40, 60, 80])

    ax.tick_params(axis="both", length=0)
    ax.grid(visible=False, axis="x")

    set_figsize = [width * 0.49, height * 0.55]

    ax.legend(loc="lower left", bbox_to_anchor=(1, 0, 1, 1))

    fig.set_size_inches(*set_figsize)
    # fig.tight_layout(pad=0.1, rect=(0.3, 0, 0.7, 1))
    fig.tight_layout(pad=0.1)

    fig_name = "motivation_pcie_bw"

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


def plot_motivation_cache(data_dir: Path, dest_dir: Path, opts: dict) -> None:
    fig, ax = plt.subplots()

    l1d_access = 21_339 / 10
    l1d_miss = 1_357 / 10
    l2_access = 1357 / 10
    l2_miss = 747 / 10

    labels = ["L1d", "L2"]

    miss_ratio = [l1d_miss / l1d_access * 100, l2_miss / l2_access * 100]

    ax.bar(
        labels,
        miss_ratio,
        fill=bar_fill,
        hatch=hatch_list[2],
        edgecolor=palette[2],
    )

    ax.set_ylabel(r"Miss ratio (\%)")

    ax.set_yticks([0, 20, 40, 60])

    ax.tick_params(axis="both", length=0)
    ax.grid(visible=False, axis="x")

    set_figsize = [width * 0.49, height * 0.55]

    fig.set_size_inches(*set_figsize)
    fig.tight_layout(pad=0.1, rect=(0.2, 0, 0.8, 1))

    fig_name = "motivation_cache"

    plt.savefig(dest_dir / f"{fig_name}.pdf")

    if opts.get("save_png", False):
        plt.savefig(dest_dir / f"{fig_name}.png")


@click.command()
@click.argument("data_dir")
@click.argument("plot_dir")
@click.option("--pick", help="Plot only the specified plot")
@click.option("--png", is_flag=True, help="Also save plots as PNG")
def main(data_dir, plot_dir, pick, png):
    data_dir = Path(data_dir)
    plot_dir = Path(plot_dir)

    plot_dir.mkdir(parents=True, exist_ok=True)

    opts = {"save_png": png}

    plots: list[Callable[[Path, Path, dict], None]] = []
    if pick is None:
        plots = [
            plot_rate_vs_cores,
            plot_rate_vs_cores_vs_pkt_sizes,
            plot_maglev,
            # plot_pcie_bw,
            plot_rtt_vs_load_reactive_notif,
            plot_rtt_vs_load_pref_notif,
            plot_rate_vs_nb_pipes,
            # plot_mica_throughput,
            # plot_mica_latency,
            # plot_log_monitor,
            # plot_motivation_pcie_bw,
            # plot_motivation_cache,
        ]
    else:
        function_name = f"plot_{pick}"
        if function_name not in globals():
            raise RuntimeError(f'Can\'t plot "{pick}"')
        plots = [globals()[function_name]]

    for plot in plots:
        plot(data_dir, plot_dir, opts)


if __name__ == "__main__":
    main()
