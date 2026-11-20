import sys, os, torch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from df.config import config
def load_df3(cfg_dir, ckpt=None):
    config.load(os.path.join(cfg_dir, 'config.ini'), config_must_exist=True, allow_defaults=True, allow_reload=True)
    from df.deepfilternet3 import init_model
    m = init_model()
    if ckpt:
        sd = torch.load(ckpt, map_location='cpu')
        if isinstance(sd, dict) and 'model' in sd and not any(k.startswith('enc.') for k in sd): sd = sd['model']
        missing, unexpected = m.load_state_dict(sd, strict=False)
        print('missing', missing, 'unexpected', unexpected)
    return m
