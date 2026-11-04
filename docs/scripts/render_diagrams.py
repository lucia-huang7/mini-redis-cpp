"""Render the README diagrams. Usage: python3 docs/scripts/render_diagrams.py [output_dir]"""
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

OUT = sys.argv[1] if len(sys.argv) > 1 else "docs"
plt.rcParams["font.family"] = ["Inter", "DejaVu Sans"]

BORDER = "#5b6b84"
ARROW = "#475569"
TEXT = "#1f2937"
FILLS = {
    "a": "#eff6ff",
    "b": "#ede9fe",
    "c": "#dbeafe",
    "d": "#ddd6fe",
    "e": "#bfdbfe",
    "f": "#c4b5fd",
}


def canvas(width, height):
    fig = plt.figure(figsize=(width / 100, height / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, width)
    ax.set_ylim(height, 0)
    ax.axis("off")
    fig.patch.set_facecolor("white")
    return fig, ax


def box(ax, x, y, w, h, text, fill, size=11.5):
    ax.add_patch(Rectangle((x, y), w, h, facecolor=FILLS[fill], edgecolor=BORDER, linewidth=1.1))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=size, color=TEXT, linespacing=1.5)


def arrow(ax, start, end, label=None, label_offset=(0, -10)):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=14, color=ARROW, linewidth=1.4))
    if label:
        mx = (start[0] + end[0]) / 2 + label_offset[0]
        my = (start[1] + end[1]) / 2 + label_offset[1]
        ax.text(mx, my, label, ha="center", va="bottom", fontsize=8.5, color="#475569")


def architecture():
    fig, ax = canvas(1600, 620)
    ax.text(800, 44, "Engineering Architecture", ha="center", va="center", fontsize=15, color=TEXT)

    y, h = 274, 92
    mid = y + h / 2
    stages = [
        (30, 140, "TCP clients", "a"),
        (232, 200, "Accept thread\nmax-clients limit", "b"),
        (494, 220, "Event loops × N\nepoll / kqueue / poll", "c"),
        (776, 226, "RESP parser\nsize limits + backpressure", "d"),
        (1064, 206, "Command execution\ndispatch + 64 shards", "e"),
    ]
    for x, w, text, fill in stages:
        box(ax, x, y, w, h, text, fill)
    for (x, w, *_), (nx, *_), label in zip(stages, stages[1:], ["connect", "assign", "bytes", "argv"]):
        arrow(ax, (x + w, mid), (nx, mid), label, label_offset=(0, -12))

    box(ax, 1330, 96, 230, 86, "Active expiry\nsampled TTL index", "b")
    box(ax, 1330, 280, 230, 98, "Ordered AOF commit\nwrite-failure guard", "f")
    box(ax, 1330, 486, 230, 80, "Append-only file", "c")
    box(ax, 1064, 486, 206, 80, "AOF rewrite\nsnapshot + delta buffer", "d")

    arrow(ax, (1270, 296), (1330, 150), "expire", label_offset=(-26, -6))
    arrow(ax, (1270, 336), (1330, 336), "writes", label_offset=(0, -10))
    arrow(ax, (1445, 378), (1445, 486), "append + fsync", label_offset=(52, -8))
    arrow(ax, (1360, 378), (1230, 486), "growth / BGREWRITEAOF", label_offset=(-78, -6))
    arrow(ax, (1270, 526), (1330, 526), "rename", label_offset=(0, -10))

    fig.savefig(f"{OUT}/engineering-architecture.png", dpi=100, facecolor="white")


def failure_paths():
    fig, ax = canvas(1600, 900)
    ax.text(808, 68, "Failure Containment Paths", ha="center", va="center", fontsize=15, color=TEXT)

    columns = [(106, 264, "Failure"), (448, 324, "Guard"), (862, 306, "Observable outcome"), (1252, 246, "Verification")]
    for x, w, title in columns:
        ax.text(x + w / 2 - 6, 201, title, ha="center", va="center", fontsize=12, fontweight="bold", color="#334155")

    rows = [
        (
            "Malformed / oversized\nRESP request",
            "Overflow-safe parser\n1 MiB request · 1,024 args\n512 KiB bulk string",
            "ERR response\noffending connection closed",
            "Hostile socket tests\nlibFuzzer CI",
        ),
        (
            "Connection pressure /\nidle or slow clients",
            "Event loops · max-clients\n30s idle sweep\n1 MiB output high-water",
            "Excess connections closed\nidle closed · reads paused",
            "64-client + limit tests\nbackpressure test",
        ),
        (
            "Short write / ENOSPC / fsync\nconcurrent writers · rewrite",
            "Commit mutex · write retry\nAOF failure latch\nblind-write records",
            "Single replay order\nMISCONF on subsequent writes",
            "Injected I/O failures\nrewrite-under-load parity",
        ),
    ]
    fills = ["a", "b", "c", "d"]
    for r, row in enumerate(rows):
        y = 256 + r * 189
        for c, text in enumerate(row):
            x, w, _ = columns[c]
            box(ax, x, y, w, 122, text, fills[c], size=10.5)
            if c < 3:
                nx = columns[c + 1][0]
                arrow(ax, (x + w, y + 61), (nx, y + 61))

    fig.savefig(f"{OUT}/failure-containment-paths.png", dpi=100, facecolor="white")


architecture()
failure_paths()
