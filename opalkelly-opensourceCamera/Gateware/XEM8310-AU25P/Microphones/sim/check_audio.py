#!/usr/bin/env python3
"""Verifie audio_words.txt : en-tetes, trous de seq <-> abandons, charge utile, horodatages."""
import sys
NS, PK = 21, 1024
words = [int(x, 16) for x in open("audio_words.txt").read().split()]
assert len(words) % PK == 0, "flux non multiple de 1024 mots : paquet tronque !"
pk = [words[i:i + PK] for i in range(0, len(words), PK)]
ok = True
prev_seq = None; prev_ts = None; gaps = 0
for p in pk:
    magic, seq, tl, th, nf, vc, vl, vh = p[:8]
    ts = tl | th << 32; vts = vl | vh << 32
    if magic != 0x4D494331 or (nf & 0xFFFF) != NS: print("en-tete invalide", seq); ok = False
    lost = (nf >> 16) & 1
    exp_lost = prev_seq is not None and seq != prev_seq + 1 or (prev_seq is None and seq != 0)
    if bool(lost) != bool(exp_lost): print(f"flag perdu incoherent seq={seq} lost={lost}"); ok = False
    if seq != (prev_seq + 1 if prev_seq is not None else 0): gaps += seq - (prev_seq + 1 if prev_seq is not None else 0)
    for i in range(NS):
        for w in range(48):
            if p[8 + i * 48 + w] != (seq * NS + i) * 48 + w: print("charge utile fausse", seq, i, w); ok = False; break
    if any(p[8 + NS * 48:]): print("bourrage non nul", seq); ok = False
    if prev_ts is not None and ts - prev_ts != 480 * NS * (seq - prev_seq): print("horodatage incoherent", seq, ts - prev_ts); ok = False
    if p[5] and vts > ts: print("vts dans le futur", seq); ok = False
    prev_seq, prev_ts = seq, ts
seqs = [p[1] for p in pk]
print(f"{len(pk)} paquets recus, seq {seqs[0]}..{seqs[-1]}, {gaps} paquet(s) abandonne(s) (trous), SOF vus a la fin : {pk[-1][5]}")
sys.exit(0 if ok and gaps > 0 else 1)
