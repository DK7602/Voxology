"""The round-3 model with BatchNorm folded and float16 weights (exactly what the C++ engine holds), in PyTorch."""
import sys; sys.path[:0] = ['/tmp/claude-0/ai/kcode', '/tmp/claude-0/ai']
import torch, retest
def model16(pt='/tmp/claude-0/ai/kout_r3/best.pt'):
    m = retest.model_from(pt); mods = dict(m.named_modules())
    for name, mod in list(m.named_modules()):
        if isinstance(mod, torch.nn.BatchNorm2d):
            pre, idx = name.rsplit('.', 1); conv = mods[pre][int(idx) - 1]
            s = mod.weight / torch.sqrt(mod.running_var + mod.eps)
            with torch.no_grad():
                w = conv.weight * (s.view(1, -1, 1, 1) if isinstance(conv, torch.nn.ConvTranspose2d) else s.view(-1, 1, 1, 1))
                b = mod.bias - mod.running_mean * s
                conv.weight.copy_(w.half().float()); mod.weight.fill_(1.0); mod.running_mean.zero_(); mod.running_var.fill_(1.0 - mod.eps); mod.bias.copy_(b.half().float())
    folded = {n for n, mo in m.named_modules() if isinstance(mo, (torch.nn.Conv2d, torch.nn.ConvTranspose2d))}
    with torch.no_grad():
        for n, p in m.named_parameters():
            mod = n.rsplit('.', 1)[0]
            if mod in folded or isinstance(mods.get(mod), torch.nn.BatchNorm2d): continue
            p.copy_(p.half().float())
        # depthwise / grouped convs before a pointwise one were not folded: round them too
        for n, mo in m.named_modules():
            if isinstance(mo, (torch.nn.Conv2d, torch.nn.ConvTranspose2d)): mo.weight.copy_(mo.weight.half().float())
    return m.eval()
