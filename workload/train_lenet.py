"""Retrain ESP's NVDLA LeNet (same prototxt) and export NVDLA compiler inputs.

ESP's demo weights were trained in DIGITS with mean-image subtraction and a
1/80 input scale that the deploy prototxt and the NVDLA int8 runtime drop, so
they reach only ~74% on MNIST under the runtime's preprocessing. This script
trains the identical architecture on the input the runtime actually feeds
(p/255, white digit on black) and writes:

  models/rl_lenet/rl_lenet.caffemodel  ESP's layer records with our blobs
                                      (training-only layers removed)
  models/rl_lenet/rl_lenet.json        int8 calibration table, same format as
                                      ESP's lenet_mnist.json (max-abs / 127)
  models/rl_lenet/lenet_mnist.prototxt copy of ESP's deploy prototxt

Usage: python workload/train_lenet.py [--epochs 8] [--seed 0]
"""
import argparse
import json
import shutil
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

from nvdla_lenet import NVDLA_DIR, REPO, RL_MODEL, LeNet, _fields, eval_mnist

OUT_DIR = RL_MODEL[0].parent
KEEP_LAYERS = ["conv1", "pool1", "conv2", "pool2", "ip1", "relu1", "ip2"]


class CaffeLeNet(nn.Module):
    """Caffe MNIST LeNet: no ReLU after the convolutions, ReLU after ip1."""

    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(1, 20, 5)
        self.conv2 = nn.Conv2d(20, 50, 5)
        self.ip1 = nn.Linear(800, 500)
        self.ip2 = nn.Linear(500, 10)

    def forward(self, x, taps=None):
        conv1 = self.conv1(x)
        pool1 = F.max_pool2d(conv1, 2)
        conv2 = self.conv2(pool1)
        pool2 = F.max_pool2d(conv2, 2)
        relu1 = F.relu(self.ip1(pool2.flatten(1)))
        ip2 = self.ip2(relu1)
        if taps is not None:
            taps.update(conv1=conv1, pool1=pool1, conv2=conv2, pool2=pool2, ip1=relu1, relu1=relu1, ip2=ip2)
        return ip2


def train(epochs, seed):
    from torchvision.datasets import MNIST
    torch.manual_seed(seed)
    ds = MNIST(REPO / "data" / "mnist", train=True, download=True)
    x = ds.data.float().unsqueeze(1) / 255.0
    y = ds.targets
    model = CaffeLeNet()
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=epochs)
    g = torch.Generator().manual_seed(seed)
    for ep in range(epochs):
        model.train()
        perm = torch.randperm(len(x), generator=g)
        total = 0.0
        for i in range(0, len(x), 128):
            idx = perm[i:i + 128]
            loss = F.cross_entropy(model(x[idx]), y[idx])
            opt.zero_grad()
            loss.backward()
            opt.step()
            total += loss.item() * len(idx)
        sched.step()
        print(f"epoch {ep + 1}/{epochs} loss {total / len(x):.4f}")
    return model, x


# --- protobuf encoding --------------------------------------------------------

def _enc_varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _enc_field(field, wire, val):
    key = _enc_varint((field << 3) | wire)
    if wire == 0:
        return key + _enc_varint(val)
    if wire == 2:
        return key + _enc_varint(len(val)) + val
    return key + val  # wire 1 / 5: raw fixed-width bytes


def _enc_blob(arr):
    arr = np.ascontiguousarray(arr, dtype="<f4")
    dims = b"".join(_enc_varint(d) for d in arr.shape)
    shape = _enc_field(1, 2, dims)                      # BlobShape.dim (packed)
    return _enc_field(7, 2, shape) + _enc_field(5, 2, arr.tobytes())


def write_caffemodel(model, template, out_path):
    """Copy ESP's layer records for KEEP_LAYERS, replacing blobs with ours."""
    sd = {k: v.detach().numpy() for k, v in model.state_dict().items()}
    layers = {}
    header = b""
    for field, wire, val in _fields(Path(template).read_bytes()):
        if field != 100:
            header += _enc_field(field, wire, val)
            continue
        name = next(v.decode() for f, _, v in _fields(val) if f == 1)
        if name not in KEEP_LAYERS:
            continue
        msg, wrote_blobs = b"", False
        for f2, w2, v2 in _fields(val):
            if f2 == 7:                                  # blobs: replace
                if not wrote_blobs:
                    for suffix in ("weight", "bias"):
                        msg += _enc_field(7, 2, _enc_blob(sd[f"{name}.{suffix}"]))
                    wrote_blobs = True
                continue
            if f2 == 3 and name == "conv1":              # bottom: 'scale' -> 'data'
                v2 = b"data"
            msg += _enc_field(f2, w2, v2)
        layers[name] = _enc_field(100, 2, msg)
    Path(out_path).write_bytes(header + b"".join(layers[n] for n in KEEP_LAYERS))


def write_calibration(model, x_calib, out_path):
    taps = {}
    with torch.no_grad():
        model(x_calib, taps)
    scale = {k: float(v.abs().max()) / 127.0 for k, v in taps.items()}
    scale["pool1"], scale["pool2"] = scale["conv1"], scale["conv2"]   # max-pool keeps the range
    scale["data"] = 1.0 / 127.0   # runtime feeds round(p*127/255) for real input p/255
    scale["prob"] = 1.0 / 127.0
    order = ["data", "conv1", "pool1", "conv2", "pool2", "relu1", "ip1", "ip2", "prob"]
    table = {k: {"scale": scale[k], "min": 0, "max": 0, "offset": 0} for k in order}
    Path(out_path).write_text(json.dumps(table, indent=4) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--calib-images", type=int, default=5000)
    args = ap.parse_args()
    torch.set_num_threads(max(1, torch.get_num_threads()))

    model, x_train = train(args.epochs, args.seed)
    model.eval()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    write_caffemodel(model, NVDLA_DIR / "lenet_mnist.caffemodel", RL_MODEL[0])
    write_calibration(model, x_train[: args.calib_images], RL_MODEL[1])
    shutil.copy(NVDLA_DIR / "lenet_mnist.prototxt", OUT_DIR / "lenet_mnist.prototxt")
    print("wrote", *(p.relative_to(REPO) for p in sorted(OUT_DIR.iterdir())))

    # Round-trip: read the exported caffemodel back and evaluate float + int8.
    acc = eval_mnist(LeNet(RL_MODEL))
    a_f, a_q = acc["native"]
    ok = a_f >= 0.99 and (a_f - a_q) <= 0.005
    print(f"exit criterion (float >= 0.99, int8 within 0.5%): {'PASS' if ok else 'FAIL'}")


if __name__ == "__main__":
    main()
