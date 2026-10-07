// Honey Tune audit: snap every note to the nearest semitone (keep drift + vibrato = move each note as one piece);
// re-analyse the render; per note: landed? shape kept? any glitches?
#include "vox/HoneyTune.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace vox;
const double sr=48000;
static double med(std::vector<double> v){ if(v.empty()) return NAN; std::sort(v.begin(),v.end()); return v[v.size()/2]; }
static double pct(std::vector<double> v,double p){ if(v.empty()) return NAN; std::sort(v.begin(),v.end()); return v[std::min(v.size()-1,size_t(p*v.size()))]; }
int main(int argc,char**argv){
  FILE* f=fopen(argv[1],"rb"); std::vector<float> x; double v; while(fread(&v,8,1,f)==1) x.push_back((float)v); fclose(f);
  auto tr=honey::analyse(x,sr,true); auto notes=honey::findNotes(tr);
  auto ed=notes; for(auto& n:ed){ n.target=std::round(n.pitch); n.edited=true; }
  auto y=honey::render(x,sr,tr,ed);
  auto to=honey::analyse(y,sr,false);
  auto at=[&](const honey::Track& t,double s)->double{ auto it=std::lower_bound(t.time.begin(),t.time.end(),s); if(it==t.time.end()||it==t.time.begin()) return 0; size_t i=it-t.time.begin(); return (t.midi[i-1]>0&&t.midi[i]>0)? t.midi[i-1]+(t.midi[i]-t.midi[i-1])*(s-t.time[i-1])/(t.time[i]-t.time[i-1]) : 0; };
  std::vector<double> land,shape,shift; int glitches=0, n=0; double secs=0;
  for(auto& q:ed){ if((q.end-q.start)/sr<0.25) continue; ++n; secs+=(q.end-q.start)/sr; double want=q.target-q.pitch; std::vector<double> out,err;
    for(size_t i=q.firstReading;i<=q.lastReading&&i<tr.time.size();++i){ double t=tr.time[i]; if(t<q.start+0.03*sr||t>q.end-0.03*sr||tr.midi[i]<=0) continue; double o=at(to,t); if(o<=0) continue;
      double e=o-(tr.midi[i]+want); e-=12*std::round(e/12); err.push_back(e*100); out.push_back(o); }
    if(err.size()<8) continue;
    double m=med(err); land.push_back(std::abs(m)); double ss=0; for(double e:err) ss+=(e-m)*(e-m); shape.push_back(std::sqrt(ss/err.size()));
    for(size_t k=1;k<err.size();++k) if(std::abs(err[k]-err[k-1])>30) ++glitches;
    shift.push_back(std::abs(want)*100); }
  // tone: band energies per note, in vs out
  std::printf("%-30s notes %3d | moved med %4.0f c | lands: err med %4.1f p90 %4.1f c | shape kept: rms dev med %4.1f p90 %4.1f c | pitch glitches %d (%.1f/min)\n",
     argv[1],n,med(shift),med(land),pct(land,0.9),med(shape),pct(shape,0.9),glitches,glitches/(secs/60));
  if(argc>2){ FILE* g=fopen(argv[2],"wb"); fwrite(y.data(),4,y.size(),g); fclose(g); }
}
