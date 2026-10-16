#!/usr/bin/env bash
# Download ESP's NVDLA LeNet demo assets into data/nvdla/ and verify checksums.
# Source: ESP third-party accelerator guide (esp.cs.columbia.edu/docs/thirdparty_acc/).
set -euo pipefail
DEST="$(cd "$(dirname "$0")/.." && pwd)/data/nvdla"
mkdir -p "$DEST"
cd "$DEST"
for f in lenet_mnist.prototxt lenet_mnist.caffemodel lenet_mnist.json lenet_mnist.nvdla seven.pgm; do
    [ -f "$f" ] || curl -sSfLO "https://esp.cs.columbia.edu/docs/thirdparty_acc/$f"
done
md5sum -c - <<'EOF'
196f1f401cdb0ff9dcf421eb1f2212fe  lenet_mnist.caffemodel
1f6b2d90212f22a6743f385ae565878b  lenet_mnist.json
9831533fd3257eea712a81ec47c95c01  lenet_mnist.nvdla
6baa3276f13ddb82d1d6fd5b94a92ba7  lenet_mnist.prototxt
a6f7cdf45dde94fe98bf7c4bf0d7b0b9  seven.pgm
EOF
