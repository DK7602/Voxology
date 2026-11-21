# AI cleanup tools (step 1 test, step 2 training)

Scratch tooling, not built by CMake. Paths inside point at the session scratch (`/tmp/claude-0/ai`).

| File | What |
| --- | --- |
| `df3.py`, `dfdsp.py` | DeepFilterNet3's DSP around the network (STFT, ERB / unit-norm features, ERB mask, deep filter, overlap-add), numpy, written from libDF. `df3.py` runs the ONNX graph. |
| `build_set.py`, `metrics.py`, `run_all.py`, `page.py`, `classic.cpp` | Step 1: the test set, measures, classic vs AI, listening page. |
| `train_sing.py` | Step 2: fine-tunes DeepFilterNet3 on singing (VocalSet 1.1), speech (VCTK), noise (MUSAN + procedural) and simulated rooms (pyroomacoustics early reflections + a stochastic tail; target = dry + 20 ms). Runs as a private Kaggle GPU kernel; `SMOKE=1` runs it locally. |
| `retest.py` | Runs a checkpoint on the step 1 set plus a held-out real noise (`noise_freesound_573577.wav` from the DeepFilterNet repo). |
| `export_onnx.py` | Exports a checkpoint to the same ONNX contract as `soniqo/DeepFilterNet3-ONNX` (`feat_erb`, `feat_spec` -> `erb_mask`, `df_coefs`); checked: the original exports bit-close to that graph. |
| `calib.py` | Recomputes a trained model's BatchNorm running averages from fresh training-style clips (the v1 run's went NaN). |
| `lossprobe.py` | Sizes of the loss terms for a model on fresh clips (to set round 2's `UNDER_W` / `LEVEL_W`). |
| `gated.py` | DeepFilterNet's own per-frame stages (local SNR > 30 dB pass through, > 20 dB no deep filter, < -10 dB silence), to test. |
| `rootcause.py`, `hfcheck.py`, `featdither.py` | Root cause of the remaining cuts: one change at a time; how often training material has a dead top end; the plug-in safety net (the AI looks at the take plus a faint hiss above 8 kHz). |
| `compare.py`, `clips2.py` | Step 2: one table (input, Voxology now, original AI, retrained AI; same measures) and the 4-version listening MP3s (1-4 beeps). |
| `retest3.py` | Round 3 retest: every case, the clean take included, with and without the hiss safety net (writes `results3_<tag>.json`). |
| `libdf.py`, `load.py` | A pure-Python stand-in for DeepFilterNet's Rust `libdf` bits the model code needs, and the loader. |

The model code (`df/`: `config.py`, `deepfilternet3.py`, `model.py`, `modules.py`, `multiframe.py`, `utils.py`) comes unchanged
from github.com/Rikorose/DeepFilterNet (v0.5.6, `DeepFilterNet/df/`; MIT / Apache-2.0), with `df/__init__.py` reduced
to `from .config import config`. The checkpoint is `models/DeepFilterNet3.zip` (`model_120.ckpt.best`) from the same repo.

Training data licences (all allow commercial use with credit): VocalSet 1.1 (Wilkins et al. 2018, CC BY 4.0), VCTK
(CSTR Edinburgh, CC BY 4.0), MUSAN noise (Snyder, Chen, Povey 2015, CC BY 4.0); plus the user's licensed Cymatics /
Looperman dry stems (the user checked: AI training allowed; private Kaggle dataset, never published). Credits go in the plug-in's about /
licence text if a trained model ships.
