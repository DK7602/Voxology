"""Exports the fine-tuned DeepFilterNet3 (a .pt state dict) for Voxology's C++ engine (dsp/src/AiCleanup.cpp):
BatchNorm folded into the convolution before it, GRU weights transposed (input-major), everything as float16.
Also a numpy reference of the network in the same layout (net_np), checked against PyTorch.
Args: <in.pt> <out.cpp>"""
import sys, numpy as np, torch
sys.path[:0] = ['/tmp/claude-0/ai/kcode', '/tmp/claude-0/ai']
EPS = 1e-5

def fold(sd, pre, bn, w_last):
    g, b, mu, var = (sd[f'{pre}.{bn}.{k}'].double().numpy() for k in ('weight', 'bias', 'running_mean', 'running_var'))
    s = g / np.sqrt(var + EPS)
    w = w_last * s.reshape((-1,) + (1,) * (w_last.ndim - 1))
    return w, b - mu * s

def tensors(sd):
    """(name, array) in the order the C++ reads them."""
    T = []; A = lambda k: sd[k].double().numpy()
    def sep(pre, dw=0, pw=1, bn=2, transposed=False):          # depthwise (freq) + pointwise + BN
        d = A(f'{pre}.{dw}.weight')                                # (64,1,1,3)
        w, b = fold(sd, pre, bn, A(f'{pre}.{pw}.weight')[:, :, 0, 0])
        T.extend([(pre + '.dw', d[:, 0, 0, :]), (pre + '.pw', w), (pre + '.b', b)])
    def gru(pre, layers):
        for l in range(layers):
            T.extend([(f'{pre}.wih{l}', A(f'{pre}.weight_ih_l{l}').T), (f'{pre}.whh{l}', A(f'{pre}.weight_hh_l{l}').T),
                      (f'{pre}.bih{l}', A(f'{pre}.bias_ih_l{l}')), (f'{pre}.bhh{l}', A(f'{pre}.bias_hh_l{l}'))])
    w, b = fold(sd, 'enc.erb_conv0', 2, A('enc.erb_conv0.1.weight')); T.extend([('erb_conv0.w', w[:, 0]), ('erb_conv0.b', b)])   # (64,3,3)
    for i in (1, 2, 3): sep(f'enc.erb_conv{i}')
    g = A('enc.df_conv0.1.weight')[:, 0]                           # (64,3,3), groups 2
    w, b = fold(sd, 'enc.df_conv0', 3, A('enc.df_conv0.2.weight')[:, :, 0, 0]); T.extend([('df_conv0.g', g), ('df_conv0.pw', w), ('df_conv0.b', b)])
    sep('enc.df_conv1')
    T.append(('df_fc_emb', A('enc.df_fc_emb.0.weight')))            # (32,96,16)
    T.append(('enc_gru.in', A('enc.emb_gru.linear_in.0.weight'))); gru('enc.emb_gru.gru', 1); T.append(('enc_gru.out', A('enc.emb_gru.linear_out.0.weight')))
    T.append(('erb_gru.in', A('erb_dec.emb_gru.linear_in.0.weight'))); gru('erb_dec.emb_gru.gru', 2); T.append(('erb_gru.out', A('erb_dec.emb_gru.linear_out.0.weight')))
    def pathway(pre):                                              # depthwise 1x1 + BN + ReLU
        w, b = fold(sd, pre, 1, A(f'{pre}.0.weight')[:, 0, 0, 0]); T.extend([(pre + '.w', w), (pre + '.b', b)])
    pathway('erb_dec.conv3p'); sep('erb_dec.convt3')
    pathway('erb_dec.conv2p'); sep('erb_dec.convt2', transposed=True)
    pathway('erb_dec.conv1p'); sep('erb_dec.convt1', transposed=True)
    pathway('erb_dec.conv0p')
    w, b = fold(sd, 'erb_dec.conv0_out', 1, A('erb_dec.conv0_out.0.weight')); T.extend([('conv0_out.w', w[0, :, 0, :]), ('conv0_out.b', b)])   # (64,3)
    g = A('df_dec.df_convp.1.weight')[:, :, :, 0]                  # (10,32,5), groups 2
    w, b = fold(sd, 'df_dec.df_convp', 3, A('df_dec.df_convp.2.weight')[:, :, 0, 0]); T.extend([('df_convp.g', g), ('df_convp.pw', w), ('df_convp.b', b)])
    T.append(('df_gru.in', A('df_dec.df_gru.linear_in.0.weight'))); gru('df_dec.df_gru.gru', 2)
    T.append(('df_skip', A('df_dec.df_skip.weight'))); T.append(('df_out', A('df_dec.df_out.0.weight')))
    return T

def f16(T): return [(n, a.astype(np.float16).astype(np.float64)) for n, a in T]

# ---- numpy reference network, one frame at a time, same layout as the C++ ----------------------------------
relu = lambda x: np.maximum(x, 0.0); sig = lambda x: 1.0 / (1.0 + np.exp(-x))
class NetNp:
    def __init__(self, T):
        self.W = dict(T); self.reset()
    def reset(self):
        self.erb_h = np.zeros((3, 32)); self.spec_h = np.zeros((3, 2, 96)); self.c0_h = np.zeros((5, 64, 96))
        self.h = {k: np.zeros(256) for k in ('enc0', 'erb0', 'erb1', 'df0', 'df1')}
    @staticmethod
    def dw(x, w, stride):                    # x (C,F), w (C,3): freq kernel 3, pad 1
        C, F = x.shape; xp = np.pad(x, ((0, 0), (1, 1))); Fo = (F - 1) // stride + 1
        return sum(w[:, k:k + 1] * xp[:, k:k + stride * (Fo - 1) + 1:stride] for k in range(3))
    @staticmethod
    def dwT(x, w):                           # transposed, stride 2, pad 1, output_padding 1: (C,F) -> (C,2F)
        C, F = x.shape; y = np.zeros((C, 2 * F))
        for i in range(F):
            for k in range(3):
                o = 2 * i - 1 + k
                if 0 <= o < 2 * F: y[:, o] += x[:, i] * w[:, k]
        return y
    def sep(self, pre, x, stride=1, transposed=False):
        W = self.W; d = self.dwT(x, W[pre + '.dw']) if transposed else self.dw(x, W[pre + '.dw'], stride)
        return relu(W[pre + '.pw'] @ d + W[pre + '.b'][:, None])
    @staticmethod
    def gl(x, w):                            # grouped linear: x (G*I), w (G,I,H)
        G, I, H = w.shape; return np.einsum('gi,gih->gh', x.reshape(G, I), w).reshape(-1)
    def gru(self, pre, l, x, key):
        W = self.W; h = self.h[key]
        gi = W[f'{pre}.bih{l}'] + x @ W[f'{pre}.wih{l}']; gh = W[f'{pre}.bhh{l}'] + h @ W[f'{pre}.whh{l}']
        r = sig(gi[:256] + gh[:256]); z = sig(gi[256:512] + gh[256:512]); n = np.tanh(gi[512:] + r * gh[512:])
        self.h[key] = (1 - z) * n + z * h; return self.h[key]
    def step(self, fe, fs):                  # fe (32,), fs (96,2) -> mask (32,), coefs (96,5,2)
        W = self.W
        self.erb_h = np.roll(self.erb_h, -1, 0); self.erb_h[-1] = fe
        self.spec_h = np.roll(self.spec_h, -1, 0); self.spec_h[-1] = fs.T
        xp = np.pad(self.erb_h, ((0, 0), (1, 1)))           # (3,34)
        w = W['erb_conv0.w']                                 # (64,3,3) [o][tau][k]
        e0 = relu(np.einsum('otk,tkf->of', w, np.stack([np.stack([xp[t, k:k + 32] for k in range(3)]) for t in range(3)])) + W['erb_conv0.b'][:, None])
        e1 = self.sep('enc.erb_conv1', e0, 2); e2 = self.sep('enc.erb_conv2', e1, 2); e3 = self.sep('enc.erb_conv3', e2, 1)
        sp = np.pad(self.spec_h, ((0, 0), (0, 0), (1, 1)))  # (3,2,98)
        g = W['df_conv0.g']                                  # (64,3,3)
        gx = np.stack([sum(g[o, t, k] * sp[t, o // 32, k:k + 96] for t in range(3) for k in range(3)) for o in range(64)])
        c0 = relu(W['df_conv0.pw'] @ gx + W['df_conv0.b'][:, None])
        c1 = self.sep('enc.df_conv1', c0, 2)
        cemb = relu(self.gl(c1.T.reshape(-1), W['df_fc_emb']))
        emb = e3.T.reshape(-1) + cemb
        x = relu(self.gl(emb, W['enc_gru.in'])); x = self.gru('enc.emb_gru.gru', 0, x, 'enc0'); emb = relu(self.gl(x, W['enc_gru.out']))
        x = relu(self.gl(emb, W['erb_gru.in'])); x = self.gru('erb_dec.emb_gru.gru', 0, x, 'erb0'); x = self.gru('erb_dec.emb_gru.gru', 1, x, 'erb1')
        y = relu(self.gl(x, W['erb_gru.out'])).reshape(8, 64).T
        p = lambda pre, e: relu(W[pre + '.w'][:, None] * e + W[pre + '.b'][:, None])
        t3 = self.sep('erb_dec.convt3', p('erb_dec.conv3p', e3) + y)
        t2 = self.sep('erb_dec.convt2', p('erb_dec.conv2p', e2) + t3, transposed=True)
        t1 = self.sep('erb_dec.convt1', p('erb_dec.conv1p', e1) + t2, transposed=True)
        s0 = p('erb_dec.conv0p', e0) + t1; s0p = np.pad(s0, ((0, 0), (1, 1)))
        mask = sig(sum(W['conv0_out.w'][:, k] @ s0p[:, k:k + 32] for k in range(3)) + W['conv0_out.b'][0])
        self.c0_h = np.roll(self.c0_h, -1, 0); self.c0_h[-1] = c0
        x = relu(self.gl(emb, W['df_gru.in'])); x = self.gru('df_dec.df_gru.gru', 0, x, 'df0'); x = self.gru('df_dec.df_gru.gru', 1, x, 'df1')
        c = x + self.gl(emb, W['df_skip'])
        gp = W['df_convp.g']                                 # (10,32,5)
        gx = np.stack([np.einsum('it,tif->f', gp[o], self.c0_h[:, (o // 5) * 32:(o // 5) * 32 + 32]) for o in range(10)])
        cp = relu(W['df_convp.pw'] @ gx + W['df_convp.b'][:, None])   # (10,96)
        o = np.tanh(self.gl(c, W['df_out'])).reshape(96, 10) + cp.T
        return mask, o.reshape(96, 5, 2)

def write_cpp(T, path, src):
    flat = np.concatenate([a.reshape(-1) for _, a in T]).astype(np.float16)
    bits = flat.view(np.uint16)
    with open(path, 'w') as f:
        f.write('// Generated by tools/ai_cleanup/export_cpp.py from ' + src + ': DeepFilterNet3 (github.com/Rikorose/DeepFilterNet,\n'
                '// MIT / Apache-2.0, (c) Hendrik Schroeter), fine-tuned for sung and rapped vocals (Voxology docs/ai-cleanup/STEP2.md).\n'
                '// float16 bits, BatchNorm folded, GRU weights input-major; order and sizes: kAiTensorSizes. Do not edit: regenerate.\n'
                '#include <cstdint>\n\nnamespace vox::ai {\n')
        f.write(f'extern const int kAiTensorCount;\nextern const int kAiTensorSizes[];\nextern const int kAiWeightCount;\nextern const std::uint16_t kAiWeights[];\n')
        f.write(f'const int kAiTensorCount = {len(T)};\nconst int kAiTensorSizes[] = {{ ' + ', '.join(str(a.size) for _, a in T) + ' };\n')
        f.write(f'const int kAiWeightCount = {bits.size};\nconst std::uint16_t kAiWeights[] = {{\n')
        for i in range(0, bits.size, 24): f.write(','.join('0x%04x' % v for v in bits[i:i + 24]) + ',\n')
        f.write('};\n\n} // namespace vox::ai\n')
    return bits.size

if __name__ == '__main__':
    import retest, dfdsp
    m = retest.model_from(sys.argv[1]); sd = m.state_dict(); T = tensors(sd)
    for n, a in T: print('%-24s %s' % (n, a.shape))
    # check: numpy net (fp64 folded, and fp16) vs PyTorch on real features
    x = retest.ld('/tmp/claude-0/ai/set/fsung_noise.f32')[:48000 * 3]
    X = dfdsp.stft(x); fe, fs = dfdsp.features(X)
    with torch.no_grad():
        _, mt, _, ct = m(torch.from_numpy(np.stack([X.real, X.imag], -1).astype(np.float32))[None, None], torch.from_numpy(fe)[None, None], torch.from_numpy(fs)[None, None])
    mt = mt[0, 0].numpy(); ct = ct[0].numpy()               # mask (T,32); coefs (5,T,96,2)
    for tag, TT in [('fp64', T), ('fp16', f16(T))]:
        net = NetNp(TT); em = ec = 0.0; Tn = X.shape[0]
        for t in range(Tn - 2):                              # the model sees feature t+2 at step t (conv lookahead 2)
            mk, cf = net.step(fe[t + 2].astype(np.float64), fs[t + 2].astype(np.float64))
            em = max(em, np.abs(mk - mt[t]).max()); ec = max(ec, np.abs(cf.transpose(1, 0, 2) - ct[:, t]).max())
        print(tag, 'max |mask err|', em, 'max |coef err|', ec)
    n = write_cpp(f16(T), sys.argv[2], sys.argv[1].split('/')[-1]); print('wrote', n, 'weights,', len(T), 'tensors')
