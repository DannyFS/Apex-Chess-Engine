#!/usr/bin/env python3
import argparse
import struct
import torch

INPUT_DIM = 64 * 10 * 64
H1 = 256
H2 = 32

Q1 = 64
Q2 = 64
Q3 = 64


def clamp_int(x, minv, maxv):
    return max(minv, min(maxv, x))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--checkpoint', required=True)
    ap.add_argument('--output', required=True)
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location='cpu')
    state = ckpt['model']

    w1 = state['embed.weight'].numpy()
    b1 = state['b1'].numpy()
    w2 = state['fc2.weight'].numpy()  # shape [H2, H1*2]
    b2 = state['fc2.bias'].numpy()
    w3 = state['fc3.weight'].numpy().reshape(-1)
    b3 = state['fc3.bias'].numpy().reshape(-1)[0]

    # Quantize
    w1_q = (w1 * Q1).round().clip(-32768, 32767).astype('int16')
    b1_q = (b1 * Q1).round().astype('int32')

    w2_t = w2.T
    w2_q = (w2_t * Q2).round().clip(-128, 127).astype('int8')
    b2_q = (b2 * Q2).round().astype('int32')

    w3_q = (w3 * Q3).round().clip(-32768, 32767).astype('int16')
    b3_q = int(round(b3 * Q3))

    with open(args.output, 'wb') as f:
        magic = b'ANNUv1\0'
        f.write(magic)
        f.write(struct.pack('<I', 1))
        f.write(struct.pack('<I', INPUT_DIM))
        f.write(struct.pack('<I', H1))
        f.write(struct.pack('<I', H2))
        f.write(struct.pack('<I', Q1))
        f.write(struct.pack('<I', Q2))
        f.write(struct.pack('<I', Q3))
        f.write(w1_q.tobytes())
        f.write(b1_q.tobytes())
        f.write(w2_q.tobytes())
        f.write(b2_q.tobytes())
        f.write(w3_q.tobytes())
        f.write(struct.pack('<i', b3_q))

    print(f"wrote {args.output}")


if __name__ == '__main__':
    main()
