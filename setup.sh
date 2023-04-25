#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

SAMPLE_CONFIG_FILE="$SCRIPT_DIR/sample_experiment_config.toml"
DEFAULT_CONFIG_FILE="$SCRIPT_DIR/experiment_config.toml"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

if [ $# -ne 1 ]; then
    echo "Must specify bitstream file or '--download'."
    echo
    echo "Usage: $0 BITSTREAM_FILE | --download"
    echo "  BITSTREAM_FILE: Path to bitstream file to use for experiments."
    echo "  --download: Automatically download bitstream file."
    exit 1
fi

cd $SCRIPT_DIR

# Make sure all submodules are cloned.
git submodule update --init --recursive

cd enso
./scripts/update_bitstream.sh $1
cd -

echo "Using python3 from $(which python3)"

# Install enso frontend.
cd enso
python3 -m pip install -e frontend
cd -

# Install requirements.
python3 -m pip install -r requirements.txt

# Initialize config.
if [ ! -f "$DEFAULT_CONFIG_FILE" ]; then
    echo "Initializing config file with default values."
    cp $SAMPLE_CONFIG_FILE $DEFAULT_CONFIG_FILE
fi

echo
printf "Edit ${GREEN}$DEFAULT_CONFIG_FILE${NC} to reflect your environment or \
specify a different config file when running experiment.py.\n"
