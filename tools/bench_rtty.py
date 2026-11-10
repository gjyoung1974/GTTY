#!/usr/bin/env python3
"""Score gtty's decoders against recordings with known text.

Usage: tools/bench_rtty.py [--limit S] [--start S] [--file WAV --mark HZ --shift HZ [--baud B] [--ref TEXT]]
                           [--] [extra gtty-cli decode options]

With --file, scores one recording of your own instead of the built-in list. Pass
"--engines all" after -- to include the codec2 engine; "--mode flutter" etc. sets
the matched-filter preset; "--corr-edge"/"--corr-accept" tune the correlator.

--score-file TEXTFILE --ref TEXT scores another program's decode of the same
recording (MMTTY, 2Tone, fldigi: save their RX window to a file) with the same
metric, so gtty's engines can be compared with them line for line.

Each benchmark lists a recording, its tone/baud settings and the reference text that
the station repeats. The decoder output is aligned to the reference with difflib and
a character error rate (substitutions + deletions + insertions over reference length)
is reported per decoder slot, plus the raw framing-error count.
"""
import difflib, re, subprocess, sys, os, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "build", "src", "cli", "gtty-cli")
FOX = "THE QUICK BROWN FOX JUMPED OVER THE LAZY DOGS BACK. TEST DE W6IWI"

BENCHES = [
    # name, file, cli options, reference line (repeated) or None for framing-only, notes
    ("40m W6IWI fading", "resources/websdr_recording_2022-03-15T05_45_16Z_7086.0kHz.wav",
     ["--mark", "2048", "--shift", "170", "--limit", "840"], FOX, "real HF, weak with QSB; station fades out after 14 min"),
    ("20m W6IWI QSB", "resources/websdr_recording_2022-06-14T23_08_37Z_14086.0kHz.wav",
     ["--mark", "2123", "--shift", "172"], FOX, "real HF with fades"),
    ("Wikipedia 10 s", "resources/rtty 10 seconds.wav",
     ["--mark", "914", "--shift", "176"], "WELCOME TO WIKIPEDIA, THE FREE ENCYCLOPEDIA THAT", "clean, 6 kHz"),
    ("DWD Hamburg I/Q", "resources/rtty_2023-02-28T10_34_55_482.wav",
     ["--iq", "--center", "1000", "--mark", "776", "--shift", "447", "--baud", "50"], None, "50 baud 450 Hz shift SDR I/Q"),
]

def decode(path, opts):
    out = subprocess.run([CLI, "decode", path] + opts, capture_output=True, text=True, timeout=1800).stdout
    slots = []
    cur = None
    for line in out.splitlines():
        m = re.match(r"== (.*?) \(chars (\d+), framing errors (\d+), AFC ([-\d.]+) Hz\) ==", line)
        if m:
            cur = {"name": m.group(1), "chars": int(m.group(2)), "ferr": int(m.group(3)), "afc": float(m.group(4)), "text": []}
            slots.append(cur)
        elif cur is not None:
            cur["text"].append(line)
    for s in slots: s["text"] = "\n".join(s["text"])
    return slots

def cer(decoded, ref):
    """Align decoded text to the repeated reference and count edits."""
    lines = [l.strip() for l in decoded.splitlines() if l.strip()]
    errs = reflen = 0
    for l in lines:
        # Beacon lines may carry a leading counter; score against the fox sentence only.
        if ref == FOX and "W6IWI" not in l and "FOX" not in l and "LAZY" not in l and len(l) < 20:
            continue
        # ignore leading counter tokens like "64 "
        l = re.sub(r"^\d+\s+", "", l)
        sm = difflib.SequenceMatcher(None, ref, l, autojunk=False)
        e = sum(max(i2 - i1, j2 - j1) for tag, i1, i2, j1, j2 in sm.get_opcodes() if tag != "equal")
        errs += e; reflen += len(ref)
    return (errs / reflen if reflen else 1.0), len(lines)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=float, default=0)
    ap.add_argument("--start", type=float, default=0)
    ap.add_argument("--file", help="score this recording instead of the built-in list")
    ap.add_argument("--mark", type=float, default=2125)
    ap.add_argument("--shift", type=float, default=170)
    ap.add_argument("--baud", type=float, default=45.45)
    ap.add_argument("--ref", help="reference text the station repeats (omit for framing-only)")
    ap.add_argument("--score-file", help="score this text file (another decoder's output) against --ref and exit")
    ap.add_argument("extra", nargs="*")
    a = ap.parse_args()
    if a.score_file:
        if not a.ref:
            sys.exit("--score-file needs --ref")
        rate, n = cer(open(a.score_file, encoding="utf-8", errors="replace").read(), a.ref)
        print(f"{os.path.basename(a.score_file):20s} {'external':16s} CER {rate*100:6.2f}%  lines {n:4d}")
        return
    benches = BENCHES
    if a.file:
        benches = [(os.path.basename(a.file)[:20], a.file,
                    ["--mark", str(a.mark), "--shift", str(a.shift), "--baud", str(a.baud)], a.ref, "user recording")]
    common = []
    if a.limit: common += ["--limit", str(a.limit)]
    if a.start: common += ["--start", str(a.start)]
    common += a.extra
    for name, path, opts, ref, note in benches:
        full = path if os.path.isabs(path) else os.path.join(ROOT, path)
        if not os.path.exists(full):
            print(f"{name:20s} (missing {path})"); continue
        slots = decode(full, opts + common)
        for s in slots:
            if ref:
                rate, n = cer(s["text"], ref)
                cerstr = f"CER {rate*100:6.2f}%  lines {n:4d}"
            else:
                cerstr = f"CER    n/a   lines {len(s['text'].splitlines()):4d}"
            print(f"{name:20s} {s['name']:16s} {cerstr}  chars {s['chars']:6d}  framing errs {s['ferr']:5d}  AFC {s['afc']:+.0f} Hz")

if __name__ == "__main__":
    main()
