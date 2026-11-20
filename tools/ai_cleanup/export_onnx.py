"""Export a DeepFilterNet3 state dict to the same ONNX contract as soniqo/DeepFilterNet3-ONNX: (feat_erb, feat_spec) -> (erb_mask, df_coefs)."""
import sys; sys.path.insert(0, '/tmp/claude-0/ai/kcode'); sys.path.insert(0, '/tmp/claude-0/ai')
import torch, numpy as np, onnxruntime as ort, retest
class Graph(torch.nn.Module):
    def __init__(self, m): super().__init__(); self.m = m
    def forward(self, feat_erb, feat_spec):
        m = self.m; fe = m.pad_feat(feat_erb); fs = m.pad_feat(feat_spec)
        e0, e1, e2, e3, emb, c0, lsnr = m.enc(fe, fs)
        mask = m.erb_dec(emb, e3, e2, e1, e0)
        coefs = m.df_out_transform(m.df_dec(emb, c0))
        return mask, coefs
m = retest.model_from(None if sys.argv[1] == '-' else sys.argv[1]); g = Graph(m).eval(); out = sys.argv[2]
T = 50; fe = torch.randn(1, 1, T, 32); fs = torch.randn(1, 2, T, 96)
torch.onnx.export(g, (fe, fs), out, input_names=['feat_erb', 'feat_spec'], output_names=['erb_mask', 'df_coefs'],
                  dynamic_axes={'feat_erb': {2: 'frames'}, 'feat_spec': {2: 'frames'}, 'erb_mask': {2: 'frames'}, 'df_coefs': {2: 'frames'}}, opset_version=17, dynamo=False)
s = ort.InferenceSession(out, providers=['CPUExecutionProvider'])
for T in (5, 137):
    fe = torch.randn(1, 1, T, 32); fs = torch.randn(1, 2, T, 96)
    with torch.no_grad(): a, b = g(fe, fs)
    oa, ob = s.run(None, {'feat_erb': fe.numpy(), 'feat_spec': fs.numpy()})
    print('frames', T, 'mask err', float(np.abs(oa - a.numpy()).max()), 'coef err', float(np.abs(ob - b.numpy()).max()), ob.shape)
