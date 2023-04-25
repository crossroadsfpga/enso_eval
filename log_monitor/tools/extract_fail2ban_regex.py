#!/usr/bin/env python3

import configparser

from pathlib import Path
from typing import Optional

import click

fail2ban_tags = {
    "<F-USER>": "",
    "</F-USER>": "",
    "<F-NOFAIL>": "",
    "</F-NOFAIL>": "",
    "<F-ALT_USER>": "",
    "</F-ALT_USER>": "",
    "<F-ALT_USER1>": "",
    "</F-ALT_USER1>": "",
    "<F-MLFFORGET>": "",
    "</F-MLFFORGET>": "",
    "<F-MLFGAINED>": "",
    "</F-MLFGAINED>": "",
    "<HOST>": "\S+",
    "<USER>": "\S+",
    "<ADDR>": "\S+",
    "<SKIPLINES>": "",
}


def replace_all_tags(line: str, replacement_tags: dict[str, str]) -> str:
    while True:
        nb_matches = 0
        for tag in replacement_tags:
            if tag not in line:
                continue
            line = line.replace(tag, replacement_tags[tag])
            nb_matches += 1
        if nb_matches == 0:
            break
    return line


def extract_fail2ban_regex_from_file(config_file: Path) -> list[str]:
    assert not config_file.is_dir()
    # Get file path
    config_base_path = config_file.parent

    config = configparser.ConfigParser()
    ret = config.read(config_file)
    assert len(ret) == 1

    before_old = None
    after_old = None

    while "INCLUDES" in config:
        includes = config["INCLUDES"]

        before = None
        after = None
        if "before" in includes:
            before = config_base_path / includes["before"]
            print("  reading before", before)
            config.read(before)

        if "after" in includes:
            after = config_base_path / includes["after"]
            print("  reading after", after)
            config.read(after)

        if before_old == before and after_old == after:
            break

        before_old = before
        after_old = after

    replacement_tags = {}
    if "DEFAULT" in config:
        default = config["DEFAULT"]
        for k, v in default.items():
            print(f" <{k}> = {v}")
            replacement_tags[f"<{k}>"] = v

    for section_name, section in config.items():
        print(section_name)
        for k, v in config.items(section_name, raw=True):
            print(f"  <{section_name}/{k}> = {v}")
            replacement_tags[f"<{section_name}/{k}>"] = v

    if "Definition" not in config:
        return []

    definition = config["Definition"]

    if "failregex" not in definition:
        return []

    for k, v in config.items("Definition", raw=True):
        replacement_tags[f"<{k}>"] = v
        replacement_tags[f"%({k})s"] = v

    unparsed_regular_expressions = definition["failregex"].splitlines()

    regular_expressions = []
    for line in unparsed_regular_expressions:
        line = replace_all_tags(line, replacement_tags)
        line = replace_all_tags(line, fail2ban_tags)

        if line.startswith("^"):
            line = line[1:]

        if line.endswith("$"):
            line = line[:-1]

        if line == "":
            continue

        regular_expressions.append(line)

    return regular_expressions


def extract_fail2ban_regex_from_dir(
    config_dir: Path, match_pattern: Optional[str]
) -> list[str]:
    if match_pattern is None:
        match_pattern = "*"

    if config_dir.is_dir():
        files = config_dir.glob(f"{match_pattern}.conf")
    else:
        files = [config_dir]

    # Make sure we do not include "common" files.
    files = [f for f in files if "common" not in f.name]

    regular_expressions = []
    for f in files:
        print(f"processing {f}")
        regular_expressions.extend(extract_fail2ban_regex_from_file(f))
    return regular_expressions


@click.command()
@click.argument(
    "config_dir",
    type=click.Path(
        exists=True, file_okay=True, dir_okay=True, path_type=Path
    ),
)
@click.argument("save_file", type=click.Path(path_type=Path))
@click.option("--match-pattern", type=str, default=None)
def main(
    config_dir: Path, save_file: Path, match_pattern: Optional[str]
) -> None:
    expressions = extract_fail2ban_regex_from_dir(config_dir, match_pattern)
    with open(save_file, "w") as f:
        for i, re in enumerate(expressions):
            f.write(f"{i}:/{re}/\n")


if __name__ == "__main__":
    main()
