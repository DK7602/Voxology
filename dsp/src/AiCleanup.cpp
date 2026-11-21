#include "vox/AiCleanup.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace vox::ai {

extern const int kAiTensorCount;
extern const int kAiTensorSizes[];
extern const int kAiWeightCount;
extern const std::uint16_t kAiWeights[];

namespace {

// ------------------------------------------------------------------------------------------------ the weights
float halfToFloat (std::uint16_t v) noexcept
{
    const std::uint32_t sign = (v & 0x8000u) << 16, expo = (v >> 10) & 0x1Fu, mant = v & 0x3FFu;
    std::uint32_t bits = 0;
    if (expo == 0)
    {
        if (mant == 0) bits = sign;
        else
        {
            int e = -1;
            std::uint32_t m = mant;
            do { ++e; m <<= 1; } while ((m & 0x400u) == 0);
            bits = sign | static_cast<std::uint32_t> (127 - 15 - e) << 23 | (m & 0x3FFu) << 13;
        }
    }
    else if (expo == 31) bits = sign | 0x7F800000u | mant << 13;
    else bits = sign | (expo + 112u) << 23 | mant << 13;
    float f;
    std::memcpy (&f, &bits, sizeof f);
    return f;
}

constexpr int C = 64;    // conv channels
constexpr int HID = 256; // GRU width
constexpr int G3 = 3 * HID;

/** The tensors in the order tools/ai_cleanup/export_cpp.py writes them (and their sizes, checked). */
struct W
{
    const float *erb0w, *erb0b;
    const float *erb1dw, *erb1pw, *erb1b, *erb2dw, *erb2pw, *erb2b, *erb3dw, *erb3pw, *erb3b;
    const float *df0g, *df0pw, *df0b, *df1dw, *df1pw, *df1b;
    const float *dfFcEmb;
    const float *encIn, *encWih, *encWhh, *encBih, *encBhh, *encOut;
    const float *erbIn, *erbWih0, *erbWhh0, *erbBih0, *erbBhh0, *erbWih1, *erbWhh1, *erbBih1, *erbBhh1, *erbOut;
    const float *p3w, *p3b, *t3dw, *t3pw, *t3b, *p2w, *p2b, *t2dw, *t2pw, *t2b, *p1w, *p1b, *t1dw, *t1pw, *t1b, *p0w, *p0b;
    const float *outW, *outB;
    const float *dfpG, *dfpPw, *dfpB;
    const float *dfIn, *dfWih0, *dfWhh0, *dfBih0, *dfBhh0, *dfWih1, *dfWhh1, *dfBih1, *dfBhh1;
    const float *dfSkip, *dfOut;
};

struct Weights
{
    std::vector<float> data;
    W w {};
    bool ok = false;

    Weights()
    {
        static constexpr int expect[] = {
            576, 64, 192, 4096, 64, 192, 4096, 64, 192, 4096, 64, 576, 4096, 64, 192, 4096, 64, 49152,
            8192, 196608, 196608, 768, 768, 8192,
            8192, 196608, 196608, 768, 768, 196608, 196608, 768, 768, 8192,
            64, 64, 192, 4096, 64, 64, 64, 192, 4096, 64, 64, 64, 192, 4096, 64, 64, 64,
            192, 1, 1600, 100, 10,
            16384, 196608, 196608, 768, 768, 196608, 196608, 768, 768,
            8192, 15360 };
        constexpr int count = static_cast<int> (sizeof expect / sizeof expect[0]);
        if (kAiTensorCount != count) return;
        int total = 0;
        for (int i = 0; i < count; ++i)
        {
            if (kAiTensorSizes[i] != expect[i]) return;
            total += expect[i];
        }
        if (total != kAiWeightCount) return;
        data.resize (static_cast<size_t> (kAiWeightCount));
        for (size_t i = 0; i < data.size(); ++i) data[i] = halfToFloat (kAiWeights[i]);
        const float* p = data.data();
        int t = 0;
        auto next = [&] { const float* r = p; p += expect[t++]; return r; };
        w.erb0w = next(); w.erb0b = next();
        w.erb1dw = next(); w.erb1pw = next(); w.erb1b = next();
        w.erb2dw = next(); w.erb2pw = next(); w.erb2b = next();
        w.erb3dw = next(); w.erb3pw = next(); w.erb3b = next();
        w.df0g = next(); w.df0pw = next(); w.df0b = next();
        w.df1dw = next(); w.df1pw = next(); w.df1b = next();
        w.dfFcEmb = next();
        w.encIn = next(); w.encWih = next(); w.encWhh = next(); w.encBih = next(); w.encBhh = next(); w.encOut = next();
        w.erbIn = next(); w.erbWih0 = next(); w.erbWhh0 = next(); w.erbBih0 = next(); w.erbBhh0 = next();
        w.erbWih1 = next(); w.erbWhh1 = next(); w.erbBih1 = next(); w.erbBhh1 = next(); w.erbOut = next();
        w.p3w = next(); w.p3b = next(); w.t3dw = next(); w.t3pw = next(); w.t3b = next();
        w.p2w = next(); w.p2b = next(); w.t2dw = next(); w.t2pw = next(); w.t2b = next();
        w.p1w = next(); w.p1b = next(); w.t1dw = next(); w.t1pw = next(); w.t1b = next();
        w.p0w = next(); w.p0b = next();
        w.outW = next(); w.outB = next();
        w.dfpG = next(); w.dfpPw = next(); w.dfpB = next();
        w.dfIn = next(); w.dfWih0 = next(); w.dfWhh0 = next(); w.dfBih0 = next(); w.dfBhh0 = next();
        w.dfWih1 = next(); w.dfWhh1 = next(); w.dfBih1 = next(); w.dfBhh1 = next();
        w.dfSkip = next(); w.dfOut = next();
        // The 64 x 64 pointwise weights transposed (input-major) for pointwise64.
        for (const float* q : { w.erb1pw, w.erb2pw, w.erb3pw, w.df0pw, w.df1pw, w.t3pw, w.t2pw, w.t1pw })
        {
            float* m = data.data() + (q - data.data());
            for (int r = 0; r < 64; ++r)
                for (int c = r + 1; c < 64; ++c) std::swap (m[r * 64 + c], m[c * 64 + r]);
        }
        ok = true;
    }
};

const Weights& weights()
{
    static const Weights instance;   // unpacked once per process (thread-safe), on first use (prepare)
    return instance;
}

// ------------------------------------------------------------------------------------------------ layers
inline float relu (float x) noexcept { return x > 0.0f ? x : 0.0f; }
inline float sigm (float x) noexcept { return 1.0f / (1.0f + std::exp (-x)); }
void reluAll (float* x, int count) noexcept { for (int i = 0; i < count; ++i) x[i] = relu (x[i]); }

/** Depthwise convolution over frequency: kernel 3, padding 1, stride 1 or 2. x [ch][f] -> y [ch][(f - 1) / stride + 1]. */
void depthwise (const float* x, int ch, int f, const float* w, int stride, float* y) noexcept
{
    const int fo = (f - 1) / stride + 1;
    for (int c = 0; c < ch; ++c)
    {
        const float* xc = x + c * f;
        float* yc = y + c * fo;
        const float w0 = w[3 * c], w1 = w[3 * c + 1], w2 = w[3 * c + 2];
        for (int o = 0; o < fo; ++o)
        {
            const int k = o * stride;
            float s = w1 * xc[k];
            if (k > 0) s += w0 * xc[k - 1];
            if (k + 1 < f) s += w2 * xc[k + 1];
            yc[o] = s;
        }
    }
}

/** Transposed depthwise convolution over frequency: kernel 3, stride 2, padding 1, output padding 1. [ch][f] -> [ch][2f]. */
void depthwiseUp (const float* x, int ch, int f, const float* w, float* y) noexcept
{
    const int fo = 2 * f;
    for (int c = 0; c < ch; ++c)
    {
        const float* xc = x + c * f;
        float* yc = y + c * fo;
        std::fill (yc, yc + fo, 0.0f);
        for (int i = 0; i < f; ++i)
            for (int k = 0; k < 3; ++k)
            {
                const int o = 2 * i - 1 + k;
                if (o >= 0 && o < fo) yc[o] += xc[i] * w[3 * c + k];
            }
    }
}

/** Pointwise (1 x 1) convolution + bias: x [cin][f] -> y [cout][f]; w [cout][cin]. (For few channels out.) */
void pointwise (const float* x, int cin, int f, const float* w, const float* b, int cout, float* __restrict y) noexcept
{
    for (int o = 0; o < cout; ++o)
    {
        float* __restrict yo = y + o * f;
        std::fill (yo, yo + f, b[o]);
        for (int i = 0; i < cin; ++i)
        {
            const float wv = w[o * cin + i];
            const float* __restrict xi = x + i * f;
            for (int k = 0; k < f; ++k) yo[k] += wv * xi[k];
        }
    }
}

/** The same with 64 channels out and the weights transposed (wt [cin][64]): the inner loop runs over the 64 outputs,
    which keeps it fast for the short frequency rows (8, 16 bins). */
void pointwise64 (const float* x, int cin, int f, const float* wt, const float* b, float* __restrict y) noexcept
{
    for (int k = 0; k < f; ++k)
    {
        float acc[64];
        std::copy (b, b + 64, acc);
        for (int i = 0; i < cin; ++i)
        {
            const float xv = x[i * f + k];
            const float* __restrict wr = wt + i * 64;
            for (int o = 0; o < 64; ++o) acc[o] += xv * wr[o];
        }
        for (int o = 0; o < 64; ++o) y[o * f + k] = acc[o];
    }
}

/** Grouped linear: x [g * in] -> y [g * out]; w [g][in][out]. */
void grouped (const float* x, int groups, int in, int out, const float* w, float* __restrict y) noexcept
{
    std::fill (y, y + groups * out, 0.0f);
    for (int g = 0; g < groups; ++g)
        for (int i = 0; i < in; ++i)
        {
            const float xv = x[g * in + i];
            const float* __restrict wr = w + (g * in + i) * out;
            float* __restrict yg = y + g * out;
            for (int k = 0; k < out; ++k) yg[k] += xv * wr[k];
        }
}

/** One GRU layer step (PyTorch's gates r, z, n); weights input-major [in][3 * HID]. h updated in place. */
void gru (const float* x, const float* wih, const float* whh, const float* bih, const float* bhh, float* h,
          float* __restrict gi, float* __restrict gh) noexcept
{
    std::copy (bih, bih + G3, gi);
    std::copy (bhh, bhh + G3, gh);
    for (int j = 0; j < HID; ++j)
    {
        const float xv = x[j], hv = h[j];
        const float* __restrict wi = wih + j * G3;
        const float* __restrict wh = whh + j * G3;
        for (int k = 0; k < G3; ++k) { gi[k] += xv * wi[k]; gh[k] += hv * wh[k]; }
    }
    for (int k = 0; k < HID; ++k)
    {
        const float r = sigm (gi[k] + gh[k]), z = sigm (gi[HID + k] + gh[HID + k]);
        const float nn = std::tanh (gi[2 * HID + k] + r * gh[2 * HID + k]);
        h[k] = (1.0f - z) * nn + z * h[k];
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------ FFT
void Fft::prepare (int size)
{
    n = std::max (size, 1);
    radix.clear(); span.clear();
    int left = n, p = 4;
    while (left > 1)
    {
        while (left % p != 0)
        {
            p = p == 4 ? 2 : p == 2 ? 3 : p + 2;
            if (p * p > left) p = left;
        }
        left /= p;
        radix.push_back (p);
        span.push_back (left);
    }
    tw.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        tw[static_cast<size_t> (i)] = std::polar (1.0, -2.0 * std::numbers::pi * i / n);
    int most = 1;
    for (const int r : radix) most = std::max (most, r);
    scratch.assign (static_cast<size_t> (most), {});
}

void Fft::forward (const std::complex<double>* in, std::complex<double>* out) noexcept
{
    if (n == 1) { out[0] = in[0]; return; }
    work (out, in, 1, 0);
}

void Fft::work (std::complex<double>* out, const std::complex<double>* in, size_t fstride, size_t stage) noexcept
{
    const int p = radix[stage], m = span[stage];
    const auto pm = static_cast<size_t> (p), ms = static_cast<size_t> (m);
    if (m == 1)
        for (size_t q = 0; q < pm; ++q) out[q] = in[q * fstride];
    else
        for (size_t q = 0; q < pm; ++q) work (out + q * ms, in + q * fstride, fstride * pm, stage + 1);

    using cd = std::complex<double>;
    if (p == 2)
    {
        for (size_t k = 0; k < ms; ++k)
        {
            const cd t = out[k + ms] * tw[k * fstride];
            out[k + ms] = out[k] - t;
            out[k] += t;
        }
    }
    else if (p == 4)
    {
        for (size_t k = 0; k < ms; ++k)
        {
            const cd s0 = out[k + ms] * tw[k * fstride], s1 = out[k + 2 * ms] * tw[2 * k * fstride], s2 = out[k + 3 * ms] * tw[3 * k * fstride];
            const cd s5 = out[k] - s1, a = out[k] + s1, s3 = s0 + s2, s4 = s0 - s2;
            out[k] = a + s3;
            out[k + 2 * ms] = a - s3;
            out[k + ms] = cd (s5.real() + s4.imag(), s5.imag() - s4.real());
            out[k + 3 * ms] = cd (s5.real() - s4.imag(), s5.imag() + s4.real());
        }
    }
    else if (p == 3)
    {
        const double e = tw[fstride * ms].imag();   // sin (-2 pi / 3)
        for (size_t k = 0; k < ms; ++k)
        {
            const cd s1 = out[k + ms] * tw[k * fstride], s2 = out[k + 2 * ms] * tw[2 * k * fstride];
            const cd s3 = s1 + s2, s0 = (s1 - s2) * e;
            const cd mid = out[k] - s3 * 0.5;
            out[k] += s3;
            out[k + 2 * ms] = cd (mid.real() + s0.imag(), mid.imag() - s0.real());
            out[k + ms] = cd (mid.real() - s0.imag(), mid.imag() + s0.real());
        }
    }
    else
    {
        const auto nn = static_cast<size_t> (n);
        for (size_t u = 0; u < ms; ++u)
        {
            for (size_t q = 0; q < pm; ++q) scratch[q] = out[u + q * ms];
            for (size_t q1 = 0; q1 < pm; ++q1)
            {
                const size_t k = u + q1 * ms;
                cd acc = scratch[0];
                size_t idx = 0;
                for (size_t q = 1; q < pm; ++q)
                {
                    idx += fstride * k;
                    idx %= nn;
                    acc += scratch[q] * tw[idx];
                }
                out[k] = acc;
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ the network
Net::Net()
{
    erbHist.assign (3 * kErb, 0.0f);
    specHist.assign (3 * 2 * kDf, 0.0f);
    c0Hist.assign (static_cast<size_t> (5 * C * kDf), 0.0f);
    for (auto& v : h) v.assign (HID, 0.0f);
    e0.assign (C * 32, 0.0f); e1.assign (C * 16, 0.0f); e2.assign (C * 8, 0.0f); e3.assign (C * 8, 0.0f);
    c0.assign (C * kDf, 0.0f); c1.assign (C * 48, 0.0f);
    tmp.assign (C * kDf, 0.0f); tmp2.assign (C * kDf, 0.0f); flat.assign (C * 48, 0.0f);
    emb.assign (512, 0.0f); x256.assign (HID, 0.0f); gi.assign (G3, 0.0f); gh.assign (G3, 0.0f); y512.assign (512, 0.0f);
    t3.assign (C * 8, 0.0f); t2.assign (C * 16, 0.0f); t1.assign (C * 32, 0.0f);
    cp.assign (10 * kDf, 0.0f); dfo.assign (10 * kDf, 0.0f);
}

bool Net::ok() const noexcept { return weights().ok; }

void Net::reset() noexcept
{
    std::fill (erbHist.begin(), erbHist.end(), 0.0f);
    std::fill (specHist.begin(), specHist.end(), 0.0f);
    std::fill (c0Hist.begin(), c0Hist.end(), 0.0f);
    for (auto& v : h) std::fill (v.begin(), v.end(), 0.0f);
    erbPos = specPos = c0Pos = 0;
}

void Net::begin (const float* erb, const float* specIn) noexcept
{
    // History: the newest frame goes in at pos; frame t (0 = oldest of K) is at (pos + 1 + t) % K.
    std::copy (erb, erb + kErb, erbHist.data() + erbPos * kErb);
    for (int k = 0; k < kDf; ++k)
    {
        specHist[static_cast<size_t> ((specPos * 2) * kDf + k)] = specIn[2 * k];
        specHist[static_cast<size_t> ((specPos * 2 + 1) * kDf + k)] = specIn[2 * k + 1];
    }
    erbAt = erbPos;
    specAt = specPos;
    erbPos = (erbPos + 1) % 3;
    specPos = (specPos + 1) % 3;
}

void Net::stage (int piece, float* maskOut, float* coefsOut) noexcept
{
    const Weights& ws = weights();
    if (! ws.ok) return;
    const W& w = ws.w;
    switch (piece)
    {
        case 0:   // the encoder's convolutions
        {
            // ---- encoder
            // erb_conv0: 1 -> 64 channels, kernel 3 (time) x 3 (freq) over the last 3 frames.
            for (int o = 0; o < C; ++o)
            {
                float* yo = e0.data() + o * 32;
                std::fill (yo, yo + 32, w.erb0b[o]);
                for (int t = 0; t < 3; ++t)
                {
                    const float* x = erbHist.data() + ((erbAt + 1 + t) % 3) * kErb;
                    const float* k3 = w.erb0w + (o * 3 + t) * 3;
                    for (int f = 0; f < 32; ++f)
                    {
                        float s = k3[1] * x[f];
                        if (f > 0) s += k3[0] * x[f - 1];
                        if (f < 31) s += k3[2] * x[f + 1];
                        yo[f] += s;
                    }
                }
            }
            reluAll (e0.data(), C * 32);
            depthwise (e0.data(), C, 32, w.erb1dw, 2, tmp.data());  pointwise64 (tmp.data(), C, 16, w.erb1pw, w.erb1b, e1.data()); reluAll (e1.data(), C * 16);
            depthwise (e1.data(), C, 16, w.erb2dw, 2, tmp.data());  pointwise64 (tmp.data(), C, 8, w.erb2pw, w.erb2b, e2.data());  reluAll (e2.data(), C * 8);
            depthwise (e2.data(), C, 8, w.erb3dw, 1, tmp.data());   pointwise64 (tmp.data(), C, 8, w.erb3pw, w.erb3b, e3.data());  reluAll (e3.data(), C * 8);

            // df_conv0: 2 -> 64 channels in 2 groups (real -> 0..31, imaginary -> 32..63), 3 x 3, then pointwise.
            for (int o = 0; o < C; ++o)
            {
                float* yo = tmp.data() + o * kDf;
                std::fill (yo, yo + kDf, 0.0f);
                const int part = o / 32;
                for (int t = 0; t < 3; ++t)
                {
                    const float* x = specHist.data() + (((specAt + 1 + t) % 3) * 2 + part) * kDf;
                    const float* k3 = w.df0g + (o * 3 + t) * 3;
                    for (int f = 0; f < kDf; ++f)
                    {
                        float s = k3[1] * x[f];
                        if (f > 0) s += k3[0] * x[f - 1];
                        if (f < kDf - 1) s += k3[2] * x[f + 1];
                        yo[f] += s;
                    }
                }
            }
            pointwise64 (tmp.data(), C, kDf, w.df0pw, w.df0b, c0.data()); reluAll (c0.data(), C * kDf);
            depthwise (c0.data(), C, kDf, w.df1dw, 2, tmp.data()); pointwise64 (tmp.data(), C, 48, w.df1pw, w.df1b, c1.data()); reluAll (c1.data(), C * 48);

            // Embedding: c1 as [freq][channel] -> grouped linear 3072 -> 512 (+ ReLU), added to e3 as [freq][channel].
            for (int c = 0; c < C; ++c)
                for (int f = 0; f < 48; ++f) flat[static_cast<size_t> (f * C + c)] = c1[static_cast<size_t> (c * 48 + f)];
            grouped (flat.data(), 32, 96, 16, w.dfFcEmb, emb.data());
            reluAll (emb.data(), 512);
            for (int c = 0; c < C; ++c)
                for (int f = 0; f < 8; ++f) emb[static_cast<size_t> (f * C + c)] += e3[static_cast<size_t> (c * 8 + f)];
            break;
        }
        case 1:   // the encoder's GRU
        {
            grouped (emb.data(), 16, 32, 16, w.encIn, x256.data()); reluAll (x256.data(), HID);
            gru (x256.data(), w.encWih, w.encWhh, w.encBih, w.encBhh, h[0].data(), gi.data(), gh.data());
            grouped (h[0].data(), 16, 16, 32, w.encOut, emb.data()); reluAll (emb.data(), 512);   // emb: the encoder's output

            break;
        }
        case 2:   // the ERB decoder's first GRU layer
        {
            // ---- ERB decoder: the band gains
            grouped (emb.data(), 16, 32, 16, w.erbIn, x256.data()); reluAll (x256.data(), HID);
            gru (x256.data(), w.erbWih0, w.erbWhh0, w.erbBih0, w.erbBhh0, h[1].data(), gi.data(), gh.data());
            break;
        }
        case 3:   // its second, then up to the band gains
        {
            gru (h[1].data(), w.erbWih1, w.erbWhh1, w.erbBih1, w.erbBhh1, h[2].data(), gi.data(), gh.data());
            grouped (h[2].data(), 16, 16, 32, w.erbOut, y512.data()); reluAll (y512.data(), 512);
            // Pathways (per-channel scale + bias + ReLU) added to what comes up.
            const auto path = [] (const float* e, int f, const float* pw, const float* pb, float* out) noexcept
            {
                for (int c = 0; c < C; ++c)
                    for (int k = 0; k < f; ++k) out[c * f + k] = relu (pw[c] * e[c * f + k] + pb[c]);
            };
            path (e3.data(), 8, w.p3w, w.p3b, tmp.data());
            for (int c = 0; c < C; ++c)
                for (int f = 0; f < 8; ++f) tmp[static_cast<size_t> (c * 8 + f)] += y512[static_cast<size_t> (f * C + c)];
            depthwise (tmp.data(), C, 8, w.t3dw, 1, tmp2.data()); pointwise64 (tmp2.data(), C, 8, w.t3pw, w.t3b, t3.data()); reluAll (t3.data(), C * 8);
            path (e2.data(), 8, w.p2w, w.p2b, tmp.data());
            for (int i = 0; i < C * 8; ++i) tmp[static_cast<size_t> (i)] += t3[static_cast<size_t> (i)];
            depthwiseUp (tmp.data(), C, 8, w.t2dw, tmp2.data()); pointwise64 (tmp2.data(), C, 16, w.t2pw, w.t2b, t2.data()); reluAll (t2.data(), C * 16);
            path (e1.data(), 16, w.p1w, w.p1b, tmp.data());
            for (int i = 0; i < C * 16; ++i) tmp[static_cast<size_t> (i)] += t2[static_cast<size_t> (i)];
            depthwiseUp (tmp.data(), C, 16, w.t1dw, tmp2.data()); pointwise64 (tmp2.data(), C, 32, w.t1pw, w.t1b, t1.data()); reluAll (t1.data(), C * 32);
            path (e0.data(), 32, w.p0w, w.p0b, tmp.data());
            for (int i = 0; i < C * 32; ++i) tmp[static_cast<size_t> (i)] += t1[static_cast<size_t> (i)];
            for (int f = 0; f < kErb; ++f)
            {
                float s = w.outB[0];
                for (int c = 0; c < C; ++c)
                {
                    const float* x = tmp.data() + c * 32;
                    const float* k3 = w.outW + c * 3;
                    s += k3[1] * x[f];
                    if (f > 0) s += k3[0] * x[f - 1];
                    if (f < 31) s += k3[2] * x[f + 1];
                }
                maskOut[f] = sigm (s);
            }

            break;
        }
        case 4:   // the DF decoder's first GRU layer
        {
            // ---- DF decoder: the low bins' filter taps
            std::copy (c0.begin(), c0.end(), c0Hist.begin() + c0Pos * C * kDf);
            c0At = c0Pos;
            c0Pos = (c0Pos + 1) % 5;
            grouped (emb.data(), 8, 64, 32, w.dfIn, x256.data()); reluAll (x256.data(), HID);
            gru (x256.data(), w.dfWih0, w.dfWhh0, w.dfBih0, w.dfBhh0, h[3].data(), gi.data(), gh.data());
            break;
        }
        default:  // its second, then the filter taps
        {
            gru (h[3].data(), w.dfWih1, w.dfWhh1, w.dfBih1, w.dfBhh1, h[4].data(), gi.data(), gh.data());
            grouped (emb.data(), 16, 32, 16, w.dfSkip, x256.data());
            for (int k = 0; k < HID; ++k) x256[static_cast<size_t> (k)] += h[4][static_cast<size_t> (k)];
            // df_convp: 64 -> 10 channels in 2 groups (32 in, 5 out each), 5 frames x 1 bin, then pointwise.
            for (int o = 0; o < 10; ++o)
            {
                float* __restrict yo = tmp.data() + o * kDf;
                std::fill (yo, yo + kDf, 0.0f);
                const int base = (o / 5) * 32;
                for (int i = 0; i < 32; ++i)
                    for (int t = 0; t < 5; ++t)
                    {
                        const float wv = w.dfpG[(o * 32 + i) * 5 + t];
                        const float* __restrict x = c0Hist.data() + ((c0At + 1 + t) % 5) * C * kDf + (base + i) * kDf;
                        for (int f = 0; f < kDf; ++f) yo[f] += wv * x[f];
                    }
            }
            pointwise (tmp.data(), 10, kDf, w.dfpPw, w.dfpB, 10, cp.data()); reluAll (cp.data(), 10 * kDf);
            grouped (x256.data(), 16, 16, 60, w.dfOut, dfo.data());
            for (int f = 0; f < kDf; ++f)
                for (int j = 0; j < 10; ++j)
                    coefsOut[f * 10 + j] = std::tanh (dfo[static_cast<size_t> (f * 10 + j)]) + cp[static_cast<size_t> (j * kDf + f)];
            break;
        }
    }
}

void Net::step (const float* erb, const float* specIn, float* maskOut, float* coefsOut) noexcept
{
    begin (erb, specIn);
    for (int k = 0; k < kStages; ++k) stage (k, maskOut, coefsOut);
}

} // namespace vox::ai

// ==================================================================================================== AiCleanup
namespace vox {

namespace {
constexpr int kModelBins = 481;   // 0 - 24 kHz in 50 Hz bins (what the network was trained on: 48 kHz, 960 points)
constexpr int kErbWidths[ai::Net::kErb] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 5, 5, 7, 7, 8, 10, 12, 13, 15,
                                            18, 20, 24, 28, 31, 37, 42, 50, 56, 67 };
constexpr double kAlpha = 0.99;    // DeepFilterNet's running averages (1 s at 10 ms frames)
constexpr int kWarmFrames = 6;     // pass-through frames before the network starts (the spectra ring fills)
constexpr int kGlideFrames = 10;   // on / off / strength glide
constexpr double kHissDecayDb = 0.06;   // the voice-level tracker's peak falls 6 dB a second
}

bool AiCleanup::supports (double sampleRate) noexcept
{
    const double perFrame = sampleRate / 50.0;
    const auto n = static_cast<long> (std::lround (perFrame));
    return sampleRate >= 16000.0 && std::abs (perFrame - static_cast<double> (n)) < 1e-9 && n % 2 == 0 && n <= 8192;
}

void AiCleanup::prepare (double sampleRate, int numChannels)
{
    sr = sampleRate;
    channels = std::clamp (numChannels, 1, kMaxChannels);
    supported = supports (sr) && net.ok();
    n = supports (sr) ? static_cast<int> (std::lround (sr / 50.0)) : 2 * static_cast<int> (std::lround (sr / 100.0));
    hop = n / 2;
    bins = n / 2 + 1;
    latency = 2 * n + n / 2;   // 2 frames + the hop the network takes to think
    fft.prepare (n);
    window.resize (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double s = std::sin (std::numbers::pi * (i + 0.5) / n);
        window[static_cast<size_t> (i)] = std::sin (0.5 * std::numbers::pi * s * s);
    }
    for (int c = 0; c < kMaxChannels; ++c)
    {
        const auto cs = static_cast<size_t> (c);
        frameIn[cs].assign (static_cast<size_t> (n), 0.0);
        hopIn[cs].assign (static_cast<size_t> (hop), 0.0);
        ola[cs].assign (static_cast<size_t> (n), 0.0);
        outFifo[cs].assign (static_cast<size_t> (hop), 0.0);
        dry[cs].assign (static_cast<size_t> (latency + 1), 0.0);
        spec[cs].assign (static_cast<size_t> (ai::Net::kOrder * bins), {});
    }
    hissFrame.assign (static_cast<size_t> (n), 0.0);
    hopMid.assign (static_cast<size_t> (hop), 0.0);
    cin.assign (static_cast<size_t> (n), {});
    cout.assign (static_cast<size_t> (n), {});
    featSpec.assign (static_cast<size_t> (kModelBins), {});
    erbEma.assign (ai::Net::kErb, 0.0);
    unitEma.assign (ai::Net::kDf, 0.0);
    bandOf.assign (static_cast<size_t> (kModelBins), 0);
    bandWidth.assign (ai::Net::kErb, 0);
    for (int b = 0, k = 0; b < ai::Net::kErb; ++b)
    {
        bandWidth[static_cast<size_t> (b)] = kErbWidths[b];
        for (int i = 0; i < kErbWidths[b]; ++i) bandOf[static_cast<size_t> (k++)] = b;
    }
    featErb.assign (ai::Net::kErb, 0.0f);
    featDf.assign (2 * ai::Net::kDf, 0.0f);
    mask.assign (ai::Net::kErb, 1.0f);
    coefs.assign (2 * ai::Net::kOrder * ai::Net::kDf, 0.0f);

    // The hiss: a 6th-order Butterworth high-pass at 8 kHz, twice (as the tested filtfilt), its level measured once.
    static constexpr double kQ[3] = { 0.51763809020504, 0.70710678118655, 1.93185165257814 };
    const double w0 = 2.0 * std::numbers::pi * std::min (kHissFromHz, 0.45 * sr) / sr, cw = std::cos (w0), sw = std::sin (w0);
    for (int i = 0; i < 6; ++i)
    {
        const double alpha = sw / (2.0 * kQ[i % 3]);
        hissHp[static_cast<size_t> (i)].setCoefficients ((1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }
    rng = 0x9E3779B97F4A7C15ull; haveSpare = false;
    for (auto& f : hissHp) f.reset();
    double sum = 0.0;
    const int measure = static_cast<int> (sr);
    for (int i = 0; i < measure + 2000; ++i)
    {
        double v = nextGauss();
        for (auto& f : hissHp) v = f.process (v);
        if (i >= 2000) sum += v * v;
    }
    // Unit power for 8 - 24 kHz as at 48 kHz: the same level per Hz at any rate.
    const double band = std::max (sr / 2.0 - kHissFromHz, 1000.0);
    hissNorm = std::sqrt (band / (24000.0 - kHissFromHz)) / std::sqrt (std::max (sum / measure, 1e-30));
    reset();
}

void AiCleanup::reset() noexcept
{
    for (int c = 0; c < kMaxChannels; ++c)
    {
        const auto cs = static_cast<size_t> (c);
        std::fill (frameIn[cs].begin(), frameIn[cs].end(), 0.0);
        std::fill (ola[cs].begin(), ola[cs].end(), 0.0);
        std::fill (outFifo[cs].begin(), outFifo[cs].end(), 0.0);
        std::fill (dry[cs].begin(), dry[cs].end(), 0.0);
        std::fill (spec[cs].begin(), spec[cs].end(), std::complex<double> {});
    }
    std::fill (hissFrame.begin(), hissFrame.end(), 0.0);
    pos = dryPos = specPos = 0;
    state = State::idle;
    stateFrames = netWarm = 0;
    amount = 0.0;
    haveCoefs = stepping = false;
    nextStage = 0;
    passFrames = 0;
    goalAmount = 0.0;
    lastChannels = channels;
    net.reset();
    rng = 0x9E3779B97F4A7C15ull; haveSpare = false;
    for (auto& f : hissHp) f.reset();
    voicePow = std::pow (10.0, -22.0 / 10.0);   // a typical home take until it has heard one
    peakDb = -22.0;
    sumIn = sumOut = 0.0;
}

double AiCleanup::nextGauss() noexcept
{
    if (haveSpare) { haveSpare = false; return spare; }
    const auto uniform = [this]
    {
        rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
        return (static_cast<double> ((rng * 0x2545F4914F6CDD1Dull) >> 11) + 1.0) / 9007199254740993.0;   // (0, 1]
    };
    const double u1 = uniform(), u2 = uniform();
    const double r = std::sqrt (-2.0 * std::log (u1)), a = 2.0 * std::numbers::pi * u2;
    spare = r * std::sin (a);
    haveSpare = true;
    return r * std::cos (a);
}

double AiCleanup::hissLevelDb() const noexcept
{
    return std::clamp (10.0 * std::log10 (voicePow + 1e-30) - kHissUnderVoiceDb, -90.0, -30.0);
}

double AiCleanup::takeReductionDb() noexcept
{
    const double d = sumOut > 0.0 ? std::max (sumIn / sumOut, -60.0) : 0.0;   // (sumIn: dB summed over sumOut frames)
    sumIn = sumOut = 0.0;
    return d;
}

void AiCleanup::process (double* const* ch, int nch, int len, double* const* removed) noexcept
{
    nch = std::clamp (nch, 1, channels);
    if (nch > lastChannels)   // a channel coming back: nothing stale from before in it
    {
        for (int c = lastChannels; c < nch; ++c)
        {
            const auto cs = static_cast<size_t> (c);
            for (auto* v : { &frameIn[cs], &hopIn[cs], &ola[cs], &outFifo[cs], &dry[cs] }) std::fill (v->begin(), v->end(), 0.0);
            std::fill (spec[cs].begin(), spec[cs].end(), std::complex<double> {});
        }
    }
    lastChannels = nch;
    const int dlen = latency + 1;
    for (int i = 0; i < len; ++i)
    {
        double mid = 0.0;
        for (int c = 0; c < nch; ++c)
        {
            const auto cs = static_cast<size_t> (c);
            const double x = ch[c][i];
            mid += x;
            hopIn[cs][static_cast<size_t> (pos)] = x;
            dry[cs][static_cast<size_t> (dryPos)] = x;
            int r = dryPos + 1; if (r >= dlen) r = 0;
            const double delayed = dry[cs][static_cast<size_t> (r)];
            const double y = state == State::idle || state == State::warming ? delayed : outFifo[cs][static_cast<size_t> (pos)];
            ch[c][i] = y;
            if (removed != nullptr) removed[c][i] = delayed - y;
        }
        hopMid[static_cast<size_t> (pos)] = mid / nch;
        if (++dryPos >= dlen) dryPos = 0;
        if (++pos >= hop)
        {
            pos = 0;
            channelsNow = nch;
            frame();
        }
        else if (stepping && nextStage < ai::Net::kStages && pos >= nextStage * hop / ai::Net::kStages)
            net.stage (nextStage++, mask.data(), coefs.data());   // (one piece of the network's frame per stretch of the hop)
    }
}

void AiCleanup::startNet() noexcept
{
    net.reset();
    netWarm = 0;
    haveCoefs = stepping = false;
    nextStage = 0;
    amount = 0.0;
    for (int b = 0; b < ai::Net::kErb; ++b) erbEma[static_cast<size_t> (b)] = -60.0 - 30.0 * b / (ai::Net::kErb - 1);
    for (int k = 0; k < ai::Net::kDf; ++k) unitEma[static_cast<size_t> (k)] = 0.001 - 0.0009 * k / (ai::Net::kDf - 1);
    // A full frame of hiss, so the first frame it judges already has it.
    const double g = std::pow (10.0, hissLevelDb() / 20.0) * hissNorm;
    for (auto& v : hissFrame)
    {
        double x = nextGauss();
        for (auto& f : hissHp) x = f.process (x);
        v = hissOn ? x * g : 0.0;
    }
}

void AiCleanup::startNowForTests() noexcept
{
    state = State::running;
    startNet();
    amount = goalAmount = 1.0;
}

void AiCleanup::features() noexcept
{
    const auto N = static_cast<size_t> (n), H = static_cast<size_t> (hop);
    const int nch = channelsNow;
    const int have = std::min (bins, kModelBins);
    // What the network hears: the mid, plus the safety net's hiss (its own spectrum: the FFT is linear).
    for (int k = 0; k < kModelBins; ++k)
    {
        std::complex<double> m {};
        if (k < have)
            for (int c = 0; c < nch; ++c) m += spec[static_cast<size_t> (c)][static_cast<size_t> (specPos * bins + k)];
        featSpec[static_cast<size_t> (k)] = m / static_cast<double> (nch);
    }
    if (hissOn)
    {
        std::copy (hissFrame.begin() + static_cast<std::ptrdiff_t> (H), hissFrame.end(), hissFrame.begin());
        const double g = std::pow (10.0, hissLevelDb() / 20.0) * hissNorm;
        for (size_t i = N - H; i < N; ++i)
        {
            double x = nextGauss();
            for (auto& f : hissHp) x = f.process (x);
            hissFrame[i] = x * g;
        }
        for (size_t i = 0; i < N; ++i) cin[i] = { hissFrame[i] * window[i], 0.0 };
        fft.forward (cin.data(), cout.data());
        for (int k = 0; k < have; ++k) featSpec[static_cast<size_t> (k)] += cout[static_cast<size_t> (k)] / static_cast<double> (n);
    }
    // Below 48 kHz the top bins don't exist (44.1 kHz: 22 - 24 kHz): it hears them mirrored from just below, so its top band
    // isn't read as a hole (empty, it heard noise there as half gone and took out about half as much). Never in the sound.
    for (int k = have; k < kModelBins; ++k)
        featSpec[static_cast<size_t> (k)] = featSpec[static_cast<size_t> (std::max (0, 2 * (have - 1) - k))];
    // 32 ERB band levels (dB) minus their running average, / 40.
    for (int b = 0, k = 0; b < ai::Net::kErb; ++b)
    {
        double e = 0.0;
        const int w = bandWidth[static_cast<size_t> (b)];
        for (int i = 0; i < w; ++i, ++k) e += std::norm (featSpec[static_cast<size_t> (k)]);
        const double d = 10.0 * std::log10 (e / w + 1e-10);
        auto& avg = erbEma[static_cast<size_t> (b)];
        avg = kAlpha * avg + (1.0 - kAlpha) * d;
        featErb[static_cast<size_t> (b)] = static_cast<float> ((d - avg) / 40.0);
    }
    // The lowest 96 bins divided by the square root of their running magnitude.
    for (int k = 0; k < ai::Net::kDf; ++k)
    {
        const auto x = featSpec[static_cast<size_t> (k)];
        auto& avg = unitEma[static_cast<size_t> (k)];
        avg = kAlpha * avg + (1.0 - kAlpha) * std::abs (x);
        const double s = 1.0 / std::sqrt (avg);
        featDf[static_cast<size_t> (2 * k)] = static_cast<float> (x.real() * s);
        featDf[static_cast<size_t> (2 * k + 1)] = static_cast<float> (x.imag() * s);
    }
}

void AiCleanup::frame() noexcept
{
    const auto N = static_cast<size_t> (n), H = static_cast<size_t> (hop);
    const int nch = channelsNow;
    for (int c = 0; c < nch; ++c)
    {
        auto& f = frameIn[static_cast<size_t> (c)];
        std::copy (f.begin() + static_cast<std::ptrdiff_t> (H), f.end(), f.begin());
        std::copy (hopIn[static_cast<size_t> (c)].begin(), hopIn[static_cast<size_t> (c)].end(), f.begin() + static_cast<std::ptrdiff_t> (N - H));
    }

    // The voice's running level (for the hiss): 10 ms frames within 30 dB of the recent peak, averaged over ~2 s.
    {
        double p = 0.0;
        for (const double v : hopMid) p += v * v;
        p /= static_cast<double> (hop);
        const double pDb = 10.0 * std::log10 (p + 1e-20);
        peakDb = std::max (pDb, peakDb - kHissDecayDb);
        if (pDb > peakDb - 30.0 && pDb > -80.0) voicePow += (1.0 - std::exp (-0.01 / 2.0)) * (p - voicePow);
    }

    // 1. The network's frame from the last hop (its pieces ran in that hop's callbacks): finish it, then the frame it
    //    judges goes out: 3 back (the network looks 2 ahead, and took a hop to think), band gains above 4.8 kHz, the
    //    5-tap filter below. The ring holds the last 5 spectra; this frame's goes in after, over the oldest.
    if (stepping)
    {
        while (nextStage < ai::Net::kStages) net.stage (nextStage++, mask.data(), coefs.data());
        stepping = false;
        haveCoefs = true;
    }
    else haveCoefs = false;
    if (state != State::idle)
    {
        const int K = ai::Net::kOrder;
        const int at = (specPos + K - 3) % K;
        const bool apply = haveCoefs && amount > 0.0;
        const int have = std::min (bins, kModelBins);
        double eIn = 0.0, eOut = 0.0;
        for (int c = 0; c < nch; ++c)
        {
            const auto& S = spec[static_cast<size_t> (c)];
            const auto* Xt = S.data() + at * bins;
            for (int k = 0; k < bins; ++k)
            {
                std::complex<double> y = Xt[k];
                if (apply)
                {
                    std::complex<double> e;
                    if (k < ai::Net::kDf)
                    {
                        e = {};
                        for (int t = 0; t < K; ++t)   // tap t: 5 - t frames back from this one's spectrum (t = 4: the newest)
                        {
                            const auto& x = S[static_cast<size_t> (((specPos + t) % K) * bins + k)];
                            const float* cf = coefs.data() + (k * K + t) * 2;
                            e += x * std::complex<double> (cf[0], cf[1]);
                        }
                    }
                    else e = Xt[k] * static_cast<double> (mask[static_cast<size_t> (k < have ? bandOf[static_cast<size_t> (k)] : ai::Net::kErb - 1)]);
                    y = (1.0 - amount) * Xt[k] + amount * e;
                }
                eIn += std::norm (Xt[k]);
                eOut += std::norm (y);
                // The inverse via the forward FFT: conjugate in, real part out (the full, Hermitian spectrum).
                cin[static_cast<size_t> (k)] = std::conj (y);
                if (k > 0 && k < n - k) cin[N - static_cast<size_t> (k)] = y;
            }
            fft.forward (cin.data(), cout.data());
            auto& o = ola[static_cast<size_t> (c)];
            for (size_t i = 0; i < N; ++i) o[i] += cout[i].real() * window[i];
            std::copy (o.begin(), o.begin() + static_cast<std::ptrdiff_t> (H), outFifo[static_cast<size_t> (c)].begin());
            std::copy (o.begin() + static_cast<std::ptrdiff_t> (H), o.end(), o.begin());
            std::fill (o.begin() + static_cast<std::ptrdiff_t> (N - H), o.end(), 0.0);
        }
        passFrames = apply ? 0 : passFrames + 1;
        // The meter: how far this frame went down (dB, a lift counts as 0), averaged until it's read.
        if (eIn > 1e-12)
        {
            sumIn += std::min (0.0, 10.0 * std::log10 (std::max (eOut, 1e-30) / eIn));
            sumOut += 1.0;
        }
        // Glide toward the strength asked for (only once the network is judging; closing glides to the input).
        if (haveCoefs || state == State::closing)
        {
            const double goal = state == State::closing ? 0.0 : goalAmount;
            amount += std::clamp (goal - amount, -1.0 / kGlideFrames, 1.0 / kGlideFrames);
            if (amount < 1e-9) amount = 0.0;
        }
    }

    // 2. On, off, warming up.
    const bool want = supported && params.on && params.strengthDb >= 0.05;
    goalAmount = ! want ? 0.0 : params.strengthDb >= kFullDb ? 1.0 : 1.0 - std::pow (10.0, -params.strengthDb / 20.0);
    switch (state)
    {
        case State::idle:
            if (! want) return;
            state = State::warming; stateFrames = 0;
            break;
        case State::warming:
            if (! want) { state = State::idle; return; }
            if (++stateFrames >= kWarmFrames) { state = State::running; startNet(); }
            break;
        case State::running:
            if (! want) state = State::closing;
            break;
        case State::closing:
            if (want) state = State::running;
            else if (passFrames >= 2) { state = State::idle; return; }   // (the last two frames out were the input itself)
            break;
    }

    // 3. This frame's spectra, over the oldest in the ring.
    for (int c = 0; c < nch; ++c)
    {
        const auto& f = frameIn[static_cast<size_t> (c)];
        for (size_t i = 0; i < N; ++i) cin[i] = { f[i] * window[i], 0.0 };
        fft.forward (cin.data(), cout.data());
        auto* X = spec[static_cast<size_t> (c)].data() + specPos * bins;
        for (int k = 0; k < bins; ++k) X[k] = cout[static_cast<size_t> (k)] / static_cast<double> (n);
    }

    // 4. What the network hears, and the first piece of its work (the rest over this hop: process()).
    if (state == State::running || state == State::closing)
    {
        features();
        if (netWarm < 2) ++netWarm;   // (as in training: its first two frames are never judged)
        else
        {
            net.begin (featErb.data(), featDf.data());
            net.stage (0, mask.data(), coefs.data());
            nextStage = 1;
            stepping = true;
        }
    }
    specPos = (specPos + 1) % ai::Net::kOrder;
}
} // namespace vox
