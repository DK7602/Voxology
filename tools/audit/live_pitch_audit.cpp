// Audit of the live Pitch (Voxology): per-hop readings + the output re-measured independently.
#include "vox/HoneyTune.h"
#include "vox/PitchCorrector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace vox;
const double sr=48000;
struct R{ double t,sung,app; int tgt; bool v; };
static double median(std::vector<double> v){ if(v.empty()) return NAN; std::sort(v.begin(),v.end()); return v[v.size()/2]; }
static double pct(std::vector<double> v,double p){ if(v.empty()) return NAN; std::sort(v.begin(),v.end()); return v[std::min(v.size()-1,size_t(p*v.size()))]; }
// 2nd-order band-pass over a series sampled at fs
static std::vector<double> bp(const std::vector<double>& x,double fs,double f0,double Q){ double w=2*M_PI*f0/fs,al=std::sin(w)/(2*Q); double b0=al/(1+al),b2=-al/(1+al),a1=-2*std::cos(w)/(1+al),a2=(1-al)/(1+al); std::vector<double> y(x.size()); double x1=0,x2=0,y1=0,y2=0; for(size_t i=0;i<x.size();++i){ double o=b0*x[i]+b2*x2-a1*y1-a2*y2; x2=x1;x1=x[i];y2=y1;y1=o;y[i]=o;} return y; }
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb"); std::vector<float> x; double v; while(fread(&v,8,1,f)==1) x.push_back((float)v); fclose(f);
  int mode=atoi(argv[2]); double speed=atof(argv[3]), hum=atof(argv[4]), amt=atof(argv[5]);
  PitchParams p; p.mode=mode; p.speedMs=speed; p.humanize=hum; p.amount=amt; p.key=0; p.scale=0;
  PitchCorrector pc; pc.setParams(p); pc.prepare(sr,1); const int lat=pc.latencySamples();
  std::vector<double> buf(128); std::vector<float> out(x.size()+lat,0.f); std::vector<R> rs;
  for(size_t s=0;s<x.size()+lat;s+=128){ size_t n=std::min<size_t>(128,x.size()+lat-s); for(size_t i=0;i<n;++i) buf[i]=s+i<x.size()?x[s+i]:0.0; double* q=buf.data(); pc.process(&q,1,int(n)); for(size_t i=0;i<n;++i) out[s+i]=float(buf[i]);
    auto r=pc.reading(); rs.push_back({r.time,r.sungMidi,r.correction,r.targetMidi,r.voiced}); }
  out.erase(out.begin(),out.begin()+lat); out.resize(x.size());
  auto tr=honey::analyse(out,sr,false);   // independent look at what came out
  auto outAt=[&](double t)->double{ auto it=std::lower_bound(tr.time.begin(),tr.time.end(),t); if(it==tr.time.end()||it==tr.time.begin()) return 0; size_t i=it-tr.time.begin(); double a=tr.midi[i-1],b=tr.midi[i]; if(a<=0||b<=0) return 0; double u=(t-tr.time[i-1])/(tr.time[i]-tr.time[i-1]); return a+(b-a)*u; };
  // held notes: voiced readings in a row with the same target, >= 0.3 s
  int notes=0,flips=0; double held=0; std::vector<double> cIntent,cReal,realErr,vibRatio,rough,warp,warpHeard; int jumps=0;
  size_t i=0;
  while(i<rs.size()){
    if(!rs[i].v){ ++i; continue; }
    size_t j=i; while(j+1<rs.size()&&rs[j+1].v) ++j;
    // within voiced run [i,j]: target flips A->B->A with B shorter than 0.2 s
    for(size_t k=i+1;k<=j;++k) if(rs[k].tgt!=rs[k-1].tgt){ size_t m=k; while(m+1<=j&&rs[m+1].tgt==rs[k].tgt) ++m; if(m+1<=j&&rs[m+1].tgt==rs[k-1].tgt&&(rs[m].t-rs[k].t)/sr<0.2) ++flips; }
    // segments of same target
    size_t a=i; while(a<=j){ size_t b=a; while(b+1<=j&&rs[b+1].tgt==rs[a].tgt) ++b;
      double dur=(rs[b].t-rs[a].t)/sr;
      if(dur>=0.3 && rs[a].tgt>=0){ ++notes; held+=dur; std::vector<double> in,re,ou; 
        for(size_t k=a;k<=b;++k){ double tt=rs[k].t; if((tt-rs[a].t)/sr<0.06) continue; double o=outAt(tt); double intent=rs[k].sung+rs[k].app; in.push_back(rs[k].sung); re.push_back(intent); ou.push_back(o>0?o:NAN);
          if(k>a && (tt-rs[a].t)/sr>0.08 && std::abs(rs[k].app-rs[k-1].app)>0.06) ++jumps;
          if(o>0){ double d=o-intent; d-=12*std::round(d/12); realErr.push_back(std::abs(d)*100);} }
        if(re.size()>10){ cIntent.push_back(std::abs(median(re)-rs[a].tgt)*100); std::vector<double> oo; for(double o:ou) if(!std::isnan(o)) oo.push_back(o); if(oo.size()>10){ double d=median(oo)-rs[a].tgt; d-=12*std::round(d/12); cReal.push_back(std::abs(d)*100);} }
        if(in.size()>40){ double ei=0,eo=0; for(size_t k=2;k<in.size();++k){ double di=in[k]-2*in[k-1]+in[k-2], dd=re[k]-2*re[k-1]+re[k-2]; ei+=di*di; eo+=dd*dd; } if(ei>1e-9) rough.push_back(std::sqrt(eo/ei)); }
        if(dur>=0.5 && in.size()>100){ double fs0=sr/128.0; // ideal: on the note + the sung pitch's own movement above ~2 Hz (zero-phase)
          std::vector<double> lp=in; double c=1-std::exp(-2*M_PI*2.0/fs0); for(int pass=0;pass<2;++pass){ double y=lp[0]; for(size_t k=0;k<lp.size();++k){ y+=(lp[k]-y)*c; lp[k]=y; } y=lp.back(); for(size_t k=lp.size();k-->0;){ y+=(lp[k]-y)*c; lp[k]=y; } }
          double e=0,eh=0; int n=0,nh=0; for(size_t k=size_t(fs0*0.1);k<in.size();++k){ double ideal=rs[a].tgt+(in[k]-lp[k]); double d=re[k]-ideal; e+=d*d; ++n; if(!std::isnan(ou[k])){ double dh=ou[k]-ideal; dh-=12*std::round(dh/12); eh+=dh*dh; ++nh; } }
          if(n>0) warp.push_back(100*std::sqrt(e/n)); if(nh>0) warpHeard.push_back(100*std::sqrt(eh/nh)); }
        if(dur>=0.5 && in.size()>100){ double fs=sr/128.0; std::vector<double> oo; for(size_t k=0;k<ou.size();++k) oo.push_back(std::isnan(ou[k])?re[k]:ou[k]);
          auto bi=bp(in,fs,5.5,1.0), bo=bp(oo,fs,5.5,1.0); double si=0,so=0; for(size_t k=size_t(fs*0.15);k<bi.size();++k){ si+=bi[k]*bi[k]; so+=bo[k]*bo[k]; } if(si>1e-6) vibRatio.push_back(std::sqrt(so/si)); }
      }
      a=b+1; }
    i=j+1; }
  double mins=x.size()/sr/60;
  std::printf("%-28s mode %d %3.0fms hum %2.0f amt %3.0f | held notes %3d (%.0f s) | centre err (intended) med %4.1f c p90 %4.1f | (heard) med %4.1f c p90 %4.1f | shifter err med %4.1f p95 %5.1f c | jumps %5.1f/min | flips %3d | vibrato kept %.2f (p10 %.2f p90 %.2f) | added roughness med %.2f p90 %.2f | WARP vs ideal med %4.1f p90 %4.1f c (heard %4.1f / %4.1f)\n",
    argv[1],mode,speed,hum,amt,notes,held,median(cIntent),pct(cIntent,0.9),median(cReal),pct(cReal,0.9),median(realErr),pct(realErr,0.95),jumps/mins,flips,median(vibRatio),pct(vibRatio,0.1),pct(vibRatio,0.9),median(rough),pct(rough,0.9),median(warp),pct(warp,0.9),median(warpHeard),pct(warpHeard,0.9));
  if(argc>6){ FILE* g=fopen(argv[6],"wb"); fwrite(out.data(),4,out.size(),g); fclose(g); }
}
