# Split each MUSDB mix with UVR's MDX-Net vocal model (what the free UVR app uses), for measuring only.
import sys, os, glob
from audio_separator.separator import Separator
root = sys.argv[1]
out = os.path.join(root, "split_wav")
os.makedirs(out, exist_ok=True)
sep = Separator(output_dir=out, output_format="WAV", output_single_stem="Vocals", model_file_dir=os.path.join(root, "models"))
sep.load_model(model_filename="UVR-MDX-NET-Voc_FT.onnx")
for f in sorted(glob.glob(os.path.join(root, "mix", "*.wav"))):
    sid = os.path.splitext(os.path.basename(f))[0]
    if glob.glob(os.path.join(out, sid + "*")):
        continue
    files = sep.separate(f)
    print(sid, files, flush=True)
