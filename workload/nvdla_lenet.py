"""Software stand-in for ESP's LeNet running on NVDLA nv_small (int8).

Loads the Caffe weights ESP ships for its NVDLA demo (lenet_mnist.caffemodel)
and emulates the int8 pipeline the NVDLA compiler builds from them with the
calibration table lenet_mnist.json:

  input   q = round(p * 127/255)                  (nvdla_runtime TestUtils.cpp,
                                                    createImageCopy)
  conv/fc int8 x int8 -> int32 accumulate, int32 bias, requantize to the
          layer's calibrated output scale with round-to-nearest + saturation
  pool    max pooling on int8 values (scale unchanged, as in the table)
  softmax runs on the CPU in the NVDLA runtime (emulator task); the output
          is quantized with the "prob" scale (~1/127)

The compiler's exact weight-scale and rounding choices are not documented, so
bit-exactness is checked empirically: with ESP's own weights (--model esp),
--check reproduces ESP's published NVDLA output for seven.pgm exactly.

Usage (assets: bash workload/fetch_esp_lenet.sh):
  python workload/nvdla_lenet.py --model esp --check        # vs ESP's NVDLA reference
  python workload/nvdla_lenet.py --eval-mnist               # retrained model accuracy
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

REPO = Path(__file__).resolve().parents[1]
NVDLA_DIR = REPO / "data" / "nvdla"
# Expected `nvdla_runtime --rawdump` output for seven.pgm (ESP third-party guide).
ESP_SEVEN_REFERENCE = [0, 2, 0, 0, 0, 0, 0, 124, 0, 0]


# --- Minimal protobuf wire-format reader for Caffe NetParameter -------------

def _varint(buf, i):
    shift = result = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, i
        shift += 7


def _fields(buf):
    """Yield (field_number, wire_type, value) for one protobuf message."""
    i = 0
    while i < len(buf):
        key, i = _varint(buf, i)
        field, wire = key >> 3, key & 7
        if wire == 0:
            val, i = _varint(buf, i)
        elif wire == 1:
            val, i = buf[i:i + 8], i + 8
        elif wire == 2:
            n, i = _varint(buf, i)
            val, i = buf[i:i + n], i + n
        elif wire == 5:
            val, i = buf[i:i + 4], i + 4
        else:
            raise ValueError(f"unsupported wire type {wire}")
        yield field, wire, val


def _blob(buf):
    """BlobProto -> np.float32 array (shape field 7, data field 5)."""
    shape, data = [], []
    for field, wire, val in _fields(buf):
        if field == 7:  # BlobShape { repeated int64 dim = 1 [packed] }
            for f2, w2, v2 in _fields(val):
                if f2 == 1 and w2 == 2:
                    j = 0
                    while j < len(v2):
                        d, j = _varint(v2, j)
                        shape.append(d)
                elif f2 == 1:
                    shape.append(v2)
        elif field == 5 and wire == 2:  # packed floats
            data.append(np.frombuffer(val, dtype="<f4"))
        elif field == 5 and wire == 5:
            data.append(np.frombuffer(val, dtype="<f4"))
        elif field in (1, 2, 3, 4) and wire == 0:  # legacy num/channels/height/width
            shape.append(val)
    arr = np.concatenate(data) if data else np.zeros(0, np.float32)
    return arr.reshape(shape) if shape else arr


def load_caffemodel(path):
    """Return {layer_name: [blob, ...]} for layers that carry weights."""
    layers = {}
    for field, _, val in _fields(Path(path).read_bytes()):
        if field != 100:  # NetParameter.layer (LayerParameter)
            continue
        name, blobs = None, []
        for f2, _, v2 in _fields(val):
            if f2 == 1:
                name = v2.decode()
            elif f2 == 7:
                blobs.append(_blob(v2))
        if blobs:
            layers[name] = blobs
    return layers


# --- LeNet in float and in emulated NVDLA int8 -------------------------------

def _q(x, scale):
    return torch.clamp(torch.round(x / scale), -128, 127)


ESP_MODEL = (NVDLA_DIR / "lenet_mnist.caffemodel", NVDLA_DIR / "lenet_mnist.json")
RL_MODEL = (REPO / "models" / "rl_lenet" / "rl_lenet.caffemodel", REPO / "models" / "rl_lenet" / "rl_lenet.json")


class LeNet:
    def __init__(self, model=RL_MODEL, weight_quant="per_channel", ip1_scale="relu1",
                 float_input_scale=1.0 / 255.0):
        caffemodel, calib = model
        w = load_caffemodel(caffemodel)
        self.calib = {k: v["scale"] for k, v in json.loads(Path(calib).read_text()).items()}
        # ESP's weights were trained in DIGITS with mean subtraction and a 1/80
        # scale that the deploy prototxt drops; the float path uses 1/256 for
        # them (Caffe MNIST default). Our retrained weights use p/255, which is
        # what the NVDLA runtime's int8 input path delivers.
        self.float_input_scale = float_input_scale
        self.params = {k: [torch.from_numpy(np.array(b, dtype=np.float32)) for b in v] for k, v in w.items()}
        self.params["ip1"][0] = self.params["ip1"][0].reshape(500, -1)
        self.params["ip2"][0] = self.params["ip2"][0].reshape(10, -1)
        self.weight_quant = weight_quant
        self.ip1_out = ip1_scale  # which calibrated scale the fused ip1+ReLU writes with

    def forward_float(self, pixels):
        x = pixels.float().unsqueeze(1) * self.float_input_scale
        p = self.params
        x = F.max_pool2d(F.conv2d(x, *p["conv1"]), 2)
        x = F.max_pool2d(F.conv2d(x, *p["conv2"]), 2)
        x = F.relu(F.linear(x.flatten(1), *p["ip1"]))
        return F.linear(x, *p["ip2"])

    def _wq(self, w):
        if self.weight_quant == "per_channel":
            s = w.abs().reshape(w.shape[0], -1).amax(1).clamp_min(1e-12) / 127.0
            s_b = s.reshape(-1, *([1] * (w.dim() - 1)))
            return torch.clamp(torch.round(w / s_b), -127, 127), s
        s = w.abs().max().clamp_min(1e-12) / 127.0
        return torch.clamp(torch.round(w / s), -127, 127), s.expand(w.shape[0])

    def _layer(self, q_in, s_in, name, s_out, op):
        w, b = self.params[name]
        qw, sw = self._wq(w)
        acc_scale = s_in * sw                                  # per output channel
        qb = torch.round(b / acc_scale)
        acc = op(q_in, qw) + qb.reshape(1, -1, *([1] * (q_in.dim() - 2 if op is F.conv2d else 0)))
        shape = (1, -1, 1, 1) if op is F.conv2d else (1, -1)
        return _q(acc * acc_scale.reshape(shape), s_out)

    def forward_int8(self, pixels):
        """pixels: uint8 tensor [N,28,28]. Returns (ip2 int8 logits, prob int8, ip2 scale)."""
        c = self.calib
        q = torch.floor(pixels.float() * 127.0 / 255.0 + 0.5).unsqueeze(1)   # runtime input
        q = self._layer(q, c["data"], "conv1", c["conv1"], F.conv2d)
        q = F.max_pool2d(q, 2)
        q = self._layer(q, c["pool1"], "conv2", c["conv2"], F.conv2d)
        q = F.max_pool2d(q, 2)
        q = self._layer(q.flatten(1), c["pool2"], "ip1", c[self.ip1_out], F.linear)
        q = torch.clamp(q, min=0)                                            # fused ReLU
        ip2 = self._layer(q, c[self.ip1_out], "ip2", c["ip2"], F.linear)
        prob = torch.softmax(ip2 * c["ip2"], dim=1)
        prob_q = torch.clamp(torch.round(prob / c["prob"]), -128, 127)
        return ip2.to(torch.int8), prob_q.to(torch.int8), c["ip2"]


def read_pgm(path):
    d = Path(path).read_bytes()
    parts = d.split(maxsplit=4)
    assert parts[0] == b"P5" and int(parts[3]) == 255
    w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(parts[4][: w * h], dtype=np.uint8).reshape(h, w)


def load_mnist_test(root=REPO / "data" / "mnist"):
    from torchvision.datasets import MNIST
    ds = MNIST(root, train=False, download=True)
    return ds.data.clone(), ds.targets.clone()   # uint8 [N,28,28] white-on-black, int64 [N]


def check_seven(net):
    img = torch.from_numpy(read_pgm(NVDLA_DIR / "seven.pgm").copy()).unsqueeze(0)
    print(f"ESP reference (NVDLA rawdump): {ESP_SEVEN_REFERENCE}")
    for label, x in (("as-is (dark on white)", img), ("inverted (white on dark)", 255 - img)):
        ip2, prob, _ = net.forward_int8(x)
        fl = torch.softmax(net.forward_float(x), 1)[0]
        print(f"{label:26s} int8 prob: {prob[0].tolist()}  argmax={int(prob[0].argmax())} | "
              f"float argmax={int(fl.argmax())} p={float(fl.max()):.3f} | ip2 int8: {ip2[0].tolist()}")


def eval_mnist(net):
    """Returns {polarity: (float_acc, int8_acc)} on the MNIST test set."""
    x, y = load_mnist_test()
    out = {}
    for label, xi in (("native", x), ("inverted", 255 - x)):
        with torch.no_grad():
            fl = net.forward_float(xi).argmax(1)
            ip2, _, _ = net.forward_int8(xi)
        a_f = (fl == y).float().mean().item()
        a_q = (ip2.float().argmax(1) == y).float().mean().item()
        out[label] = (a_f, a_q)
        print(f"MNIST {label:8s} float acc {a_f:.4f} | int8 acc {a_q:.4f} | n={len(y)}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", choices=["esp", "rl"], default="rl",
                    help="esp: ESP's demo weights; rl: our retrained weights (models/rl_lenet)")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--eval-mnist", action="store_true")
    ap.add_argument("--weight-quant", choices=["per_channel", "per_tensor"], default="per_channel")
    ap.add_argument("--ip1-scale", choices=["ip1", "relu1"], default="relu1")
    args = ap.parse_args()
    if args.model == "esp":
        net = LeNet(ESP_MODEL, args.weight_quant, args.ip1_scale, float_input_scale=0.00390625)
    else:
        net = LeNet(RL_MODEL, args.weight_quant, args.ip1_scale)
    print("layers:", {k: [tuple(b.shape) for b in v] for k, v in net.params.items()})
    if args.check:
        check_seven(net)
    if args.eval_mnist:
        eval_mnist(net)


if __name__ == "__main__":
    main()
