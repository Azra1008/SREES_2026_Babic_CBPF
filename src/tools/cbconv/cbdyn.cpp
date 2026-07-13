// =====================================================================================
//  cbdyn — standalone generator DINAMIČKOG modela (validacija DynEmit.h bez natID/dTwin).
//  Ulaz: MATPOWER .m  ->  Izlaz: .dmodl (DAE) + .vmodl (grafici).
//  Upotreba:
//    cbdyn <in.m> <out.dmodl> [distType 0=load|1=short] [distBusId] [t0] [t1] [mag]
//  Seminarski SREES — Azra Babić (19029).
// =====================================================================================
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <complex>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include "DynEmit.h"

using cplx = std::complex<double>;
static const int PQ = 1, PV = 2, SLACK = 3;
struct Bus    { int id, type; double Pd, Qd, Gs, Bs, Vm, Va; };
struct Gen    { int bus; double Pg, Qg, Qmax, Qmin, Vg; int status; };
struct Branch { int f, t; double r, x, b, ratio, angle; int status; };

static std::string stripComment(const std::string& l){ auto p=l.find('%'); return p==std::string::npos?l:l.substr(0,p); }
static std::vector<std::vector<double>> readMatrix(const std::vector<std::string>& L, size_t& i){
    std::vector<std::vector<double>> rows;
    for(; i<L.size(); ++i) if(stripComment(L[i]).find('[')!=std::string::npos) break;
    ++i;
    for(; i<L.size(); ++i){ std::string s=stripComment(L[i]); if(s.find(']')!=std::string::npos) break;
        for(char&c:s) if(c==';'||c==',') c=' ';
        std::istringstream iss(s); std::vector<double> row; double v; while(iss>>v) row.push_back(v);
        if(!row.empty()) rows.push_back(row); }
    return rows;
}

int main(int argc, char** argv){
    if(argc<3){ std::fprintf(stderr,"Upotreba: %s <in.m> <out.dmodl> [distType 0|1] [distBus] [t0] [t1] [mag]\n",argv[0]); return 1; }
    std::string inFile=argv[1], outFile=argv[2];
    dyn::Options opt;
    if(argc>3) opt.distType  = std::atoi(argv[3]);
    if(argc>4) opt.distBusId = std::atoi(argv[4]);
    if(argc>5) opt.t0        = std::atof(argv[5]);
    if(argc>6) opt.t1        = std::atof(argv[6]);
    if(argc>7) opt.magnitude = std::atof(argv[7]);

    double baseMVA=100.0; std::vector<Bus> bus; std::vector<Gen> gen; std::vector<Branch> branch;
    { std::ifstream f(inFile); if(!f){ std::fprintf(stderr,"GRESKA: ne mogu otvoriti %s\n",inFile.c_str()); return 1; }
      std::vector<std::string> L; std::string ln; while(std::getline(f,ln)) L.push_back(ln);
      for(size_t i=0;i<L.size();++i){ std::string s=stripComment(L[i]);
        if(s.find("mpc.baseMVA")!=std::string::npos){ auto eq=s.find('='); if(eq!=std::string::npos) baseMVA=std::stod(s.substr(eq+1)); }
        else if(s.find("mpc.bus")!=std::string::npos&&s.find('=')!=std::string::npos){ for(auto&r:readMatrix(L,i)) if(r.size()>=9) bus.push_back({int(r[0]),int(r[1]),r[2],r[3],r[4],r[5],r[7],r[8]}); }
        else if(s.find("mpc.gen")!=std::string::npos&&s.find('=')!=std::string::npos){ for(auto&r:readMatrix(L,i)) if(r.size()>=8) gen.push_back({int(r[0]),r[1],r[2],r[3],r[4],r[5],int(r[7])}); }
        else if(s.find("mpc.branch")!=std::string::npos&&s.find('=')!=std::string::npos){ for(auto&r:readMatrix(L,i)) if(r.size()>=11) branch.push_back({int(r[0]),int(r[1]),r[2],r[3],r[4],r[8],r[9],int(r[10])}); }
      }
    }
    if(bus.empty()){ std::fprintf(stderr,"GRESKA: nema bus podataka.\n"); return 1; }

    int n=(int)bus.size();
    dyn::Grid G; G.n=n;
    G.id.resize(n); G.type.resize(n); G.Yii.assign(n,cplx(0,0));
    G.nbr.assign(n,{}); G.Pinj.assign(n,0); G.Qinj.assign(n,0); G.Vset.assign(n,1.0);
    for(int i=0;i<n;++i){ G.id[i]=bus[i].id; G.id2idx[bus[i].id]=i; }

    std::map<int,std::vector<const Gen*>> gensAt;
    for(auto&g:gen) if(g.status>=1) gensAt[g.bus].push_back(&g);
    G.slackIdx=-1;
    for(int i=0;i<n;++i){ int t=bus[i].type; if(t==PV&&gensAt.find(bus[i].id)==gensAt.end()) t=PQ; G.type[i]=t; if(t==SLACK) G.slackIdx=i; }
    if(G.slackIdx<0){ std::fprintf(stderr,"GRESKA: nema slack cvora.\n"); return 1; }

    for(auto&br:branch){ if(br.status!=1) continue;
        int fi=G.id2idx[br.f], ti=G.id2idx[br.t];
        cplx y=(br.r!=0||br.x!=0)?cplx(1,0)/cplx(br.r,br.x):cplx(0,0);
        cplx bsh(0,br.b/2.0); double ratio=(br.ratio==0)?1.0:br.ratio;
        cplx a=std::polar(ratio,br.angle*M_PI/180.0);
        G.Yii[fi]+=(y+bsh)/(a*std::conj(a)); G.Yii[ti]+=(y+bsh);
        G.Yij[{fi,ti}]+=-y/std::conj(a); G.Yij[{ti,fi}]+=-y/a;
        G.nbr[fi].insert(ti); G.nbr[ti].insert(fi); }
    for(int i=0;i<n;++i) if(bus[i].Gs!=0||bus[i].Bs!=0) G.Yii[i]+=cplx(bus[i].Gs,bus[i].Bs)/baseMVA;

    for(int i=0;i<n;++i){ double Pg=0,Qg=0; auto it=gensAt.find(bus[i].id);
        if(it!=gensAt.end()) for(auto*g:it->second){ Pg+=g->Pg; Qg+=g->Qg; }
        G.Pinj[i]=(Pg-bus[i].Pd)/baseMVA; G.Qinj[i]=(Qg-bus[i].Qd)/baseMVA; }
    for(int i=0;i<n;++i){ auto it=gensAt.find(bus[i].id);
        if(G.type[i]==PV) G.Vset[i]=(it!=gensAt.end())?it->second[0]->Vg:bus[i].Vm;
        else if(G.type[i]==SLACK) G.Vset[i]=(it!=gensAt.end())?it->second[0]->Vg:bus[i].Vm; }
    G.Vslack=G.Vset[G.slackIdx]; G.dSlack=bus[G.slackIdx].Va*M_PI/180.0;

    // podrazumijevani čvor smetnje: prvi PQ sa opterećenjem
    if(opt.distBusId<0){ for(int i=0;i<n;++i) if(G.type[i]==PQ&&(G.Pinj[i]!=0||G.Qinj[i]!=0)){ opt.distBusId=G.id[i]; break; } }

    std::string dmodl=dyn::buildDModl(G,opt);
    std::string vmodl=dyn::buildVModl(G,opt);
    std::string vFile=outFile.substr(0,outFile.find_last_of('.'))+".vmodl";
    { std::ofstream o(outFile); if(!o){ std::fprintf(stderr,"GRESKA: ne mogu %s\n",outFile.c_str()); return 1; } o<<dmodl; }
    { std::ofstream o(vFile);   if(!o){ std::fprintf(stderr,"GRESKA: ne mogu %s\n",vFile.c_str());   return 1; } o<<vmodl; }

    int ng=0; for(int i=0;i<n;++i) if(G.type[i]==SLACK||G.type[i]==PV) ++ng;
    std::fprintf(stderr,"OK: %d cvorova, %d generatora. Smetnja: tip=%d cvor=%d t=[%g,%g] mag=%g\n",
                 n,ng,opt.distType,opt.distBusId,opt.t0,opt.t1,opt.magnitude);
    std::fprintf(stderr,"Zapisano: %s + %s\n",outFile.c_str(),vFile.c_str());
    return 0;
}
