#!/bin/bash
# Build in the same Docker image PELLETINO uses (no host ESP-IDF needed).
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 idf.py -B build_docker "${@:-build}"
