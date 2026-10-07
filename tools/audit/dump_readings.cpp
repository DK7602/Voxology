#include "vox/PitchCorrector.h"
#include <cstdio>
#include <vector>
using namespace vox;
int main(int argc,char**argv){ FILE* f=fopen(argv[1],"rb"); std::vector<double> x; double v; while(fread(&v,8,1,f)==1) x.push_back(v); fclose(f);
  PitchParams p; p.mode=atoi(argv[2]); p.speedMs=atof(argv[3]); p.humanize=atof(argv[4]); p.amount=100; p.key=0; p.scale=0;
  PitchCorrector pc; pc.setParams(p); pc.prepare(48000,1); std::vector<double> b(128);
  FILE* g=fopen(argv[5],"w");
  for(size_t s=0;s+128<=x.size();s+=128){ for(int i=0;i<128;++i) b[i]=x[s+i]; double* q=b.data(); pc.process(&q,1,128); auto r=pc.reading(); fprintf(g,"%.5f %d %.4f %.4f %d\n",r.time/48000.0,r.voiced?1:0,r.sungMidi,r.correction,r.targetMidi); }
  fclose(g); }
