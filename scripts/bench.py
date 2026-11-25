#!/usr/bin/env python3
"""Batch run of stego_cli on real photos: capacity, PSNR/SSIM, chi-square, RS, SPA.

  python3 scripts/bench.py PHOTO_DIR [--cli build/stego_cli] [--out bench.csv] [--work /tmp/stego_bench]
                           [--fills 0.05,0.25,0.5] [--configs adaptive-lsb:1,adaptive-lsb:2,block-dct]

Uses password mode (small header, so small images still fit something). Message = random bytes,
size = fill * (max message for that file/config). After every embed it also extracts and compares,
so a real-photo round-trip failure shows up as ok=0. Only the standard library is needed.
Config syntax: adaptive-lsb:1 (bits), adaptive-lsb:2, block-dct (defaults), block-dct:3 (pairs).
"""
import argparse, csv, os, re, subprocess, sys, tempfile

EXTS = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp"}

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def algo_args(cfg):
    parts = cfg.split(":")
    name = parts[0]
    if name == "adaptive-lsb":
        bits = parts[1] if len(parts) > 1 and parts[1] else "1"
        args = ["--algo", name, "--bits", bits]
        if len(parts) > 2 and parts[2]:
            args += ["--threshold", parts[2]]
        return name, args, args
    if name == "block-dct":
        cap_args = ["--algo", name]
        if len(parts) > 1 and parts[1]:
            cap_args += ["--pairs", parts[1]]
        emb_args = list(cap_args)
        if len(parts) > 2 and parts[2]:
            emb_args += ["--strength", parts[2]]
        return name, cap_args, emb_args
    raise SystemExit(f"unknown config {cfg}")

def f(rx, text, grp=1):
    m = re.search(rx, text)
    return m.group(grp) if m else ""

def parse_analyze(t):
    row = {"psnr": f(r"PSNR\s*:\s*([\d.inf]+)", t), "ssim": f(r"SSIM\s*:\s*([\d.]+)", t)}
    for tag, key in (("chi cover", "chi_p_cover"), ("chi stego", "chi_p_stego")):
        row[key] = f(rf"{tag}\s+chi2=[\d.]+ dof=-?\d+ p\(embedded\)=([\d.]+)", t)
    for algo in ("RS", "SPA"):
        for who in ("cover", "stego"):
            m = re.search(rf"(?m)^{algo}\s+{who}\s*:\s*(?:p=(-?[\d.]+)|(n/a))", t)
            row[f"{algo.lower()}_{who}"] = (m.group(1) or "n/a") if m else ""
    return row

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir"); ap.add_argument("--cli", default="build/stego_cli")
    ap.add_argument("--out", default="bench.csv"); ap.add_argument("--work", default=None)
    ap.add_argument("--fills", default="0.05,0.25,0.5")
    ap.add_argument("--bytes", type=int, default=None)
    ap.add_argument("--configs", default="adaptive-lsb:1,adaptive-lsb:2,block-dct")
    a = ap.parse_args()
    fills = [None] if a.bytes is not None else [float(x) for x in a.fills.split(",")]
    cfgs = a.configs.split(",")
    work = a.work or tempfile.mkdtemp(prefix="stego_bench_")
    os.makedirs(work, exist_ok=True)
    pw = os.path.join(work, "pw.txt"); open(pw, "w").write("correct horse battery staple\n")

    photos = sorted(p for p in os.listdir(a.dir) if os.path.splitext(p)[1].lower() in EXTS)
    if not photos: raise SystemExit("no images found")
    cols = ["photo", "config", "fill", "max_bytes", "msg_bytes", "capacity_bits", "used_pct", "psnr", "ssim",
            "chi_p_cover", "chi_p_stego", "rs_cover", "rs_stego", "spa_cover", "spa_stego", "ok", "note"]
    with open(a.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, cols); w.writeheader()
        for ph in photos:
            src = os.path.join(a.dir, ph)
            for cfg in cfgs:
                name, aa_cap, aa_emb = algo_args(cfg)
                cap = run([a.cli, "capacity", *aa_cap, "--in", src, "--mode", "password"])
                mx = int(f(r"max message:\s*(\d+)", cap.stdout) or 0)
                for fill in fills:
                    n = a.bytes if a.bytes is not None else int(mx * fill)
                    fill_val = (round(n / mx, 4) if mx > 0 else "") if a.bytes is not None else fill
                    row = {c: "" for c in cols}; row.update(photo=ph, config=cfg, fill=fill_val, max_bytes=mx)
                    if cap.returncode != 0 or n < 1 or n > mx:
                        row.update(ok=0, note=f"capacity rc={cap.returncode} max={mx} need={n}"); w.writerow(row); continue
                    msg = os.path.join(work, "msg.bin"); open(msg, "wb").write(os.urandom(n))
                    stg = os.path.join(work, "stego.png")
                    e = run([a.cli, "embed", *aa_emb, "--in", src, "--out", stg, "--password-file", pw, "--file", msg])
                    row["msg_bytes"] = n
                    if e.returncode != 0:
                        row.update(ok=0, note=f"embed rc={e.returncode}: {e.stderr.strip()[:80]}"); w.writerow(row); continue
                    row["capacity_bits"] = f(r"capacity\s*:\s*(\d+) bits", e.stdout)
                    row["used_pct"] = f(r"\(([\d.]+)%\)", e.stdout)
                    back = os.path.join(work, "back.bin")
                    x = run([a.cli, "extract", *aa_cap, "--in", stg, "--password-file", pw, "--out", back])
                    same = x.returncode == 0 and open(back, "rb").read() == open(msg, "rb").read()
                    an = run([a.cli, "analyze", "--cover", src, "--stego", stg])
                    row.update(parse_analyze(an.stdout)); row["ok"] = int(same)
                    if not same: row["note"] = f"extract rc={x.returncode}"
                    w.writerow(row); fh.flush()
                    fill_disp = f"{fill_val:4.2f}" if isinstance(fill_val, float) else " n/a"
                    print(f"{ph:28s} {cfg:14s} fill {fill_disp}  PSNR {row['psnr']:>6s}  SSIM {row['ssim']:>6s}  "
                          f"RS {row['rs_stego']:>6s}  SPA {row['spa_stego']:>6s}  ok={row['ok']}", flush=True)
    print(f"\nwritten {a.out}  (work dir {work})")

if __name__ == "__main__":
    main()
