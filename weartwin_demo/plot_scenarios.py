"""Plot helpers for the demo notebook: readiness over time with a coloured strip for the twin's state."""
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

from schema import READY_AMBER, READY_GREEN, STATES

COLORS = {"Normal": "#d9d8d0", "Fatigue": "#2a6fd6", "Thermal strain": "#e8643a", "Cardio-resp stress": "#17a67a"}


def plot_run(log, ax, title=""):
    """log: list of dicts returned by SoldierTwin.update(). Calibration windows are skipped."""
    live = [o for o in log if o["status"] == "Live"]
    t = [o["t_min"] for o in live]
    ax.axhline(READY_GREEN, color="#bbb", lw=0.8)
    ax.axhline(READY_AMBER, color="#bbb", lw=0.8)
    ax.plot(t, [o["readiness"] for o in live], color="black", lw=2)
    # state strip under the curve
    start = 0
    for i in range(1, len(live) + 1):
        if i == len(live) or live[i]["state"] != live[start]["state"]:
            ax.fill_between([t[start], t[min(i, len(t) - 1)]], -14, -4, color=COLORS[live[start]["state"]], lw=0)
            start = i
    ax.set_ylim(-16, 104)
    ax.set_yticks([0, 25, 50, 75, 100])
    ax.set_xlabel("minutes since start (first 10 = resting calibration)", fontsize=8)
    ax.set_ylabel("readiness")
    ax.set_title(title, loc="left", fontsize=10, fontweight="bold")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)


def _legend(fig):
    fig.legend(handles=[Patch(color=COLORS[s], label=s) for s in STATES], loc="lower center", ncol=4, frameon=False)


def plot_scenarios(logs, ncols=3):
    """logs: {scenario name: log}. One panel per scenario."""
    n = len(logs)
    nrows = -(-n // ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(4.6 * ncols, 3.2 * nrows), squeeze=False)
    for ax, (name, log) in zip(axes.flat, logs.items()):
        plot_run(log, ax, name)
    for ax in list(axes.flat)[n:]:
        ax.axis("off")
    _legend(fig)
    fig.tight_layout(rect=(0, 0.06, 1, 1))
    return fig


def plot_single(log, title="Your input"):
    fig, ax = plt.subplots(figsize=(8, 3.6))
    plot_run(log, ax, title)
    _legend(fig)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    return fig
