"""Render the report figures (light-mode PNGs) from reports/metrics.json and the demo replay log.
Run after train_eval.py and demo_replay.py:  python3 make_figures.py
Palette: dataviz reference palette; the three alert states use categorical slots 1-3 (validated all-pairs).
"""
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

from schema import STATES

ROOT = Path(__file__).parent
REP = ROOT / "reports"
SURFACE, INK, INK2, MUTED, GRID, AXIS = "#fcfcfb", "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
STATE_COL = {"Fatigue": BLUE, "Thermal strain": ORANGE, "Cardio-resp stress": AQUA}
IDX_COL = {"fatigue_idx": BLUE, "thermal_idx": ORANGE, "cardio_idx": AQUA}
IDX_NAME = {"fatigue_idx": "Fatigue", "thermal_idx": "Thermal strain", "cardio_idx": "Cardio-resp stress"}
SCEN = {"heavy_load_hot": "heavy load, hot", "steady_march": "steady march", "hot_dehydration": "hot march, dehydrated",
        "altitude_climb": "altitude climb", "high_intensity": "high-intensity intervals", "rest_duty": "rest / guard duty"}

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "font.family": "sans-serif", "font.size": 9, "text.color": INK, "axes.labelcolor": INK2,
    "xtick.color": MUTED, "ytick.color": MUTED, "axes.edgecolor": AXIS, "axes.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False,
})


def style(ax, ygrid=True):
    ax.tick_params(length=0, labelsize=8)
    if ygrid:
        ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)


def strip(ax, t, states, y0, h, col_map):
    """Draw a state timeline strip: grey track + coloured runs for non-Normal states."""
    ax.add_patch(plt.Rectangle((t.iloc[0], y0), t.iloc[-1] - t.iloc[0] + 0.5, h, color=GRID, lw=0))
    start = 0
    for i in range(1, len(states) + 1):
        if i == len(states) or states.iloc[i] != states.iloc[start]:
            s = states.iloc[start]
            if s in col_map:
                ax.add_patch(plt.Rectangle((t.iloc[start], y0), t.iloc[i - 1] - t.iloc[start] + 0.5, h,
                                           facecolor=col_map[s], edgecolor=SURFACE, lw=1.0))
            start = i


def load_demo():
    log = pd.read_csv(REP / "demo_replay_log.csv")
    log = log[log.status == "Live"].copy()
    truth = pd.read_csv(ROOT / "data" / "demo_squad.csv")
    truth["win"] = truth.groupby("soldier_id").cumcount()
    log["win"] = log["t_min"] / 0.5
    log["win"] = log["win"].round().astype(int)
    keep = ["soldier_id", "win", "scenario", "thermal_idx", "fatigue_idx", "cardio_idx"]
    truth = truth[keep].rename(columns={c: "true_" + c for c in keep[3:]})
    return log.merge(truth, on=["soldier_id", "win"], how="left")


def readiness_panel(ax, g, small=True):
    t = g["t_min"]
    ax.plot(t, g["true_readiness"], color=MUTED, lw=1.3, solid_capstyle="round", zorder=2)
    ax.plot(t, g["readiness"], color=INK, lw=2, solid_capstyle="round", solid_joinstyle="round", zorder=3)
    ax.set_ylim(-17, 102)
    ax.set_yticks([0, 25, 50, 75, 100])
    for y in (50, 75):
        ax.axhline(y, color=AXIS, lw=0.8, zorder=1)
    strip(ax, t, g["state"], -7, 5, STATE_COL)
    strip(ax, t, g["true_state"], -14, 5, STATE_COL)
    tr = ax.get_yaxis_transform()
    ax.text(-0.012, -4.5, "twin", transform=tr, ha="right", va="center", fontsize=7, color=MUTED)
    ax.text(-0.012, -11.5, "truth", transform=tr, ha="right", va="center", fontsize=7, color=MUTED)
    style(ax)
    ax.spines["left"].set_visible(False)
    ax.set_xlim(0, t.max() + 1)


# ---------------------------------------------------------------- figure 1: squad replay
def fig_squad(demo):
    ids = list(demo.soldier_id.unique())
    fig, axes = plt.subplots(2, 3, figsize=(12.5, 7.2), sharey=True)
    for ax, sid in zip(axes.ravel(), ids):
        g = demo[demo.soldier_id == sid]
        readiness_panel(ax, g)
        ax.set_title(f"{sid}  ·  {SCEN.get(g.scenario.iloc[0], g.scenario.iloc[0])}", loc="left", fontsize=10,
                     color=INK, fontweight="semibold", pad=8)
        if ax in axes[1]:
            ax.set_xlabel("minutes since start", fontsize=8)
    for ax in axes[:, 1:].ravel():
        ax.tick_params(labelleft=False)
    handles = [Line2D([], [], color=INK, lw=2, label="Twin readiness (0-100)"),
               Line2D([], [], color=MUTED, lw=1.3, label="True readiness"),
               Patch(facecolor=BLUE, label="Fatigue"), Patch(facecolor=ORANGE, label="Thermal strain"),
               Patch(facecolor=AQUA, label="Cardio-resp stress"), Patch(facecolor=GRID, label="Normal")]
    fig.legend(handles=handles, loc="lower center", ncol=6, frameon=False, fontsize=8.5, labelcolor=INK2,
               bbox_to_anchor=(0.5, 0.005))
    fig.suptitle("Squad replay on simulated soldiers the model never saw", x=0.012, ha="left", fontsize=14,
                 fontweight="semibold", y=0.985)
    fig.text(0.012, 0.935, "Readiness: 75+ ready, 50-75 caution, below 50 rest/rotate (hairlines). Strips show the confirmed "
             "twin state vs the ground-truth state. The first 10 min are resting calibration.", fontsize=8.5, color=INK2)
    fig.tight_layout(rect=(0, 0.045, 1, 0.925), h_pad=2.2, w_pad=1.5)
    fig.savefig(REP / "fig_squad_replay.png", dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------- figure 2: twin detail (one hit, one miss)
def fig_detail(demo, ids=("SOL-01", "SOL-03")):
    fig, axes = plt.subplots(2, 2, figsize=(12.5, 7.4), sharex="col", gridspec_kw=dict(height_ratios=[1.15, 1]))
    for j, sid in enumerate(ids):
        g = demo[demo.soldier_id == sid]
        readiness_panel(axes[0, j], g)
        axes[0, j].set_title(f"{sid}  ·  {SCEN.get(g.scenario.iloc[0])}", loc="left", fontsize=10.5, fontweight="semibold", pad=8)
        ax = axes[1, j]
        t = g["t_min"]
        for c, col in IDX_COL.items():
            ax.plot(t, g["true_" + c], color=col, lw=1.2, alpha=0.45, solid_capstyle="round")
            ax.plot(t, g[c], color=col, lw=2, solid_capstyle="round")
        ax.axhline(0.5, color=AXIS, lw=0.8)
        ax.text(t.max() + 0.6, 0.5, "alert level", fontsize=7.5, color=MUTED, va="center")
        ax.set_ylim(0, 1.02)
        ax.set_yticks([0, 0.25, 0.5, 0.75, 1.0])
        style(ax)
        ax.spines["left"].set_visible(False)
        ax.set_xlim(0, t.max() + 1)
        ax.set_xlabel("minutes since start", fontsize=8)
    axes[0, 0].set_ylabel("readiness", fontsize=8.5)
    axes[1, 0].set_ylabel("strain index (0-1)", fontsize=8.5)
    for ax in axes[:, 1]:
        ax.tick_params(labelleft=False)
    handles = [Line2D([], [], color=INK, lw=2, label="Twin readiness"), Line2D([], [], color=MUTED, lw=1.3, label="True readiness")] + \
              [Line2D([], [], color=c, lw=2, label=IDX_NAME[k]) for k, c in IDX_COL.items()] + \
              [Line2D([], [], color=INK2, lw=1.2, alpha=0.5, label="thin = ground truth")]
    fig.legend(handles=handles, loc="lower center", ncol=6, frameon=False, fontsize=8.5, labelcolor=INK2, bbox_to_anchor=(0.5, 0.005))
    fig.suptitle("Digital-twin state vector: readiness plus three strain indices", x=0.012, ha="left", fontsize=14,
                 fontweight="semibold", y=0.985)
    fig.text(0.012, 0.935, "Left: heavy load in the heat, thermal alert raised about 2 min after the ground-truth threshold. "
             "Right: hot dehydrated march, the twin's thermal index rises but stays under the alert level, so the episode is missed.",
             fontsize=8.5, color=INK2)
    fig.tight_layout(rect=(0, 0.045, 1, 0.925), h_pad=1.6, w_pad=1.5)
    fig.savefig(REP / "fig_twin_detail.png", dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------- figure 3: confusion matrices
def fig_confusion(M):
    cmap = LinearSegmentedColormap.from_list("seq", ["#cde2fb", "#2a78d6", "#0d366b"])
    short = ["Normal", "Fatigue", "Thermal", "Cardio-resp"]
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 5.4))
    for ax, key, title in [(axes[0], "clf", "Gradient-boosting twin"), (axes[1], "rules", "Hand-written threshold rules")]:
        cm = np.array(M["test"][key]["confusion"], float)
        pct = cm / cm.sum(axis=1, keepdims=True)
        ax.imshow(pct, cmap=cmap, vmin=0, vmax=1)
        for i in range(4):
            for j in range(4):
                ax.text(j, i, f"{pct[i, j] * 100:.0f}%", ha="center", va="center", fontsize=10,
                        color="white" if pct[i, j] > 0.45 else INK)
        ax.set_xticks(range(4)); ax.set_yticks(range(4))
        ax.set_xticklabels(short, fontsize=8.5, color=INK2)
        ax.set_yticklabels([f"{s}\n(n={int(cm[i].sum()):,})" for i, s in enumerate(short)], fontsize=8.5, color=INK2)
        ax.set_xlabel("predicted", fontsize=8.5)
        ax.set_title(f"{title}  ·  macro-F1 {M['test'][key]['macro_f1']:.2f}", loc="left", fontsize=10.5, fontweight="semibold", pad=10)
        ax.tick_params(length=0)
        for s in ax.spines.values():
            s.set_visible(False)
        ax.set_xticks(np.arange(-.5, 4, 1), minor=True); ax.set_yticks(np.arange(-.5, 4, 1), minor=True)
        ax.grid(which="minor", color=SURFACE, linewidth=2.5); ax.tick_params(which="minor", length=0)
    axes[0].set_ylabel("true state", fontsize=8.5)
    fig.suptitle("Held-out soldiers: share of each true state predicted as each state", x=0.012, ha="left", fontsize=14,
                 fontweight="semibold", y=0.98)
    fig.text(0.012, 0.905, f"{M['n_soldiers']['test']} soldiers never used in training; calibration windows excluded. "
             "Rows sum to 100%: the diagonal is per-class recall.", fontsize=8.5, color=INK2)
    fig.tight_layout(rect=(0, 0, 1, 0.9))
    fig.savefig(REP / "fig_confusion.png", dpi=160)
    plt.close(fig)


# ---------------------------------------------------------------- figure 4: robustness
def fig_robustness(M):
    rob = M["robustness"]
    names = list(rob.keys())
    base = [n for n in names if n == "no change"]
    rest = sorted([n for n in names if n != "no change"], key=lambda n: -rob[n]["aug"]["macro_f1"])
    order = base + rest
    pretty = {n: n.replace(" missing for whole session", " sensor lost").replace("extra noise on all sensors", "extra noise, all sensors")
              for n in order}
    y = np.arange(len(order))[::-1]
    h = 0.34
    fig, ax = plt.subplots(figsize=(10.5, 6.4))
    ax.barh(y + h / 2 + 0.02, [rob[n]["aug"]["macro_f1"] for n in order], height=h, color=BLUE)
    ax.barh(y - h / 2 - 0.02, [rob[n]["noaug"]["macro_f1"] for n in order], height=h, color=ORANGE)
    for yy, n in zip(y, order):
        if n in ("no change", "imu missing for whole session", "hr missing for whole session", "rr missing for whole session"):
            ax.text(rob[n]["aug"]["macro_f1"] + 0.008, yy + h / 2 + 0.02, f"{rob[n]['aug']['macro_f1']:.2f}", va="center", fontsize=8, color=INK2)
            ax.text(rob[n]["noaug"]["macro_f1"] + 0.008, yy - h / 2 - 0.02, f"{rob[n]['noaug']['macro_f1']:.2f}", va="center", fontsize=8, color=INK2)
    ax.set_yticks(y)
    ax.set_yticklabels([pretty[n] for n in order], fontsize=8.5, color=INK2)
    ax.set_xlim(0, 1.0)
    ax.set_xlabel("macro-F1 on held-out soldiers (state classification)", fontsize=8.5)
    ax.grid(axis="x", color=GRID, linewidth=0.8); ax.set_axisbelow(True)
    ax.tick_params(length=0, labelsize=8.5)
    ax.spines["left"].set_visible(False)
    ax.legend(handles=[Patch(facecolor=BLUE, label="trained with random sensor-loss augmentation (final model)"),
                       Patch(facecolor=ORANGE, label="trained without augmentation")],
              loc="lower right", frameon=False, fontsize=8.5, labelcolor=INK2)
    fig.suptitle("What happens when a sensor drops out for a whole session", x=0.012, ha="left", fontsize=14, fontweight="semibold", y=0.985)
    fig.text(0.012, 0.925, "Single training run per variant: differences under about 0.03 are within the fold-to-fold spread and should not be over-read.",
             fontsize=8.5, color=INK2)
    fig.tight_layout(rect=(0, 0, 1, 0.915))
    fig.savefig(REP / "fig_sensor_robustness.png", dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    M = json.load(open(REP / "metrics.json"))
    demo = load_demo()
    fig_squad(demo)
    fig_detail(demo)
    fig_confusion(M)
    fig_robustness(M)
    print("saved:", sorted(p.name for p in REP.glob("fig_*.png")))
