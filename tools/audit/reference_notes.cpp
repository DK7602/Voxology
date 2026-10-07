#include "vox/HoneyTune.h"
#include <cstdio>
#include <vector>
int main(int,char**argv){ FILE* f=fopen(argv[1],"rb"); std::vector<float> x; double v; while(fread(&v,8,1,f)==1) x.push_back((float)v); fclose(f);
 auto t=vox::honey::analyse(x,48000,true); auto n=vox::honey::findNotes(t); FILE* g=fopen(argv[2],"w"); for(auto& q:n) fprintf(g,"%.5f %.5f %.4f\n",q.start/48000,q.end/48000,q.pitch); fclose(g); }
