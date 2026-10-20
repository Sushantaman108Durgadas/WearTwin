import pandas as pd
import matplotlib.pyplot as plt

path = "hardware/output/denoised_moving.csv"
df = pd.read_csv(path)

# First 30 seconds
df = df[df["Timestamp_s"] <= 30].copy()

fig, axes = plt.subplots(2, 1, figsize=(14, 8), sharex=True)

axes[0].plot(
    df["Timestamp_s"], df["IR_Moving_Resampled"],
    label="Original moving IR", alpha=0.75
)
axes[0].plot(
    df["Timestamp_s"], df["IR_Denoised"],
    label="Autoencoder IR", alpha=0.85
)
axes[0].set_title("IR: Original vs Denoised")
axes[0].set_ylabel("ADC / signal units")
axes[0].legend()
axes[0].grid(True)

axes[1].plot(
    df["Timestamp_s"], df["RED_Moving_Resampled"],
    label="Original moving RED", alpha=0.75
)
axes[1].plot(
    df["Timestamp_s"], df["RED_Denoised"],
    label="Autoencoder RED", alpha=0.85
)
axes[1].set_title("RED: Original vs Denoised")
axes[1].set_xlabel("Time (seconds)")
axes[1].set_ylabel("ADC / signal units")
axes[1].legend()
axes[1].grid(True)

plt.tight_layout()
plt.savefig("hardware/output/denoising_comparison.png", dpi=150)
plt.show()