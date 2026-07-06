// =====================================================================================
//  Jezgra konvertora nelinearnog strujnog balansa (polarne koordinate).
//  Y-bus se gradi u natID kompleksnoj dense matrici (dense::CmplxMatrix).
//  Ulaz: MATPOWER .m  ->  Izlaz: dTwin .dmodl NL model (string).
//  Seminarski SREES — Azra Babić (19029).
// =====================================================================================
#pragma once
#include <cmath>
#include <complex>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include <atomic>
#include <functional>
#include <dense/Matrix.h>   // natID kompleksna dense matrica (dense::CmplxMatrix) za Y-bus

namespace cb {

using cplx = std::complex<double>;
static const int PQ = 1, PV = 2, SLACK = 3;

struct Bus    { int id, type; double Pd, Qd, Gs, Bs, Vm, Va; };
struct Gen    { int bus; double Pg, Qg, Qmax, Qmin, Vg; int status; };
struct Branch { int f, t; double r, x, b, ratio, angle; int status; };

class Converter
{
    double _baseMVA = 100.0;
    std::vector<Bus> _bus;
    std::vector<Gen> _gen;
    std::vector<Branch> _branch;
    std::string _err;

    static std::string stripComment(const std::string& line) {
        auto p = line.find('%');
        return (p == std::string::npos) ? line : line.substr(0, p);
    }
    static std::vector<std::vector<double>> readMatrix(const std::vector<std::string>& L, size_t& i) {
        std::vector<std::vector<double>> rows;
        for (; i < L.size(); ++i) if (stripComment(L[i]).find('[') != std::string::npos) break;
        ++i;
        for (; i < L.size(); ++i) {
            std::string s = stripComment(L[i]);
            if (s.find(']') != std::string::npos) break;
            for (char& c : s) if (c == ';' || c == ',') c = ' ';
            std::istringstream iss(s); std::vector<double> row; double v;
            while (iss >> v) row.push_back(v);
            if (!row.empty()) rows.push_back(row);
        }
        return rows;
    }
    static std::string num(double v) { std::ostringstream o; o.precision(15); o << v; return o.str(); }

public:
    const std::string& error() const { return _err; }
    int busCount() const { return (int)_bus.size(); }

    bool loadCase(const std::string& fname) {
        _bus.clear(); _gen.clear(); _branch.clear();
        std::ifstream f(fname);
        if (!f) { _err = "Ne mogu otvoriti ulaznu datoteku: " + fname; return false; }
        std::vector<std::string> L; std::string ln;
        while (std::getline(f, ln)) L.push_back(ln);
        for (size_t i = 0; i < L.size(); ++i) {
            std::string s = stripComment(L[i]);
            if (s.find("mpc.baseMVA") != std::string::npos) {
                auto eq = s.find('='); if (eq != std::string::npos) _baseMVA = std::stod(s.substr(eq + 1));
            } else if (s.find("mpc.bus") != std::string::npos && s.find('=') != std::string::npos) {
                for (auto& r : readMatrix(L, i)) if (r.size() >= 9)
                    _bus.push_back({int(r[0]),int(r[1]),r[2],r[3],r[4],r[5],r[7],r[8]});
            } else if (s.find("mpc.gen") != std::string::npos && s.find('=') != std::string::npos) {
                for (auto& r : readMatrix(L, i)) if (r.size() >= 8)
                    _gen.push_back({int(r[0]),r[1],r[2],r[3],r[4],r[5],int(r[7])});
            } else if (s.find("mpc.branch") != std::string::npos && s.find('=') != std::string::npos) {
                for (auto& r : readMatrix(L, i)) if (r.size() >= 11)
                    _branch.push_back({int(r[0]),int(r[1]),r[2],r[3],r[4],r[8],r[9],int(r[10])});
            }
        }
        if (_bus.empty()) { _err = "Nema bus podataka u ulaznoj datoteci."; return false; }
        return true;
    }

    // Generiše .dmodl tekst. onProgress(0..100) zove se iz radnog threada; cancel: prekid.
    bool convert(std::string& outText, const std::function<void(int)>& onProgress = {},
                 std::atomic<bool>* cancel = nullptr) {
        auto setP = [&](int p){ if (onProgress) onProgress(p); };
        auto stop = [&](){ return cancel && cancel->load(); };
        setP(2);

        const int n = (int)_bus.size();
        std::map<int,int> id2i; for (int i=0;i<n;++i) id2i[_bus[i].id]=i;

        std::map<int,std::vector<const Gen*>> gensAt;
        for (auto& g : _gen) if (g.status >= 1) gensAt[g.bus].push_back(&g);

        std::vector<int> type(n); int slackIdx = -1;
        for (int i=0;i<n;++i){
            int t=_bus[i].type;
            if (t==PV && gensAt.find(_bus[i].id)==gensAt.end()) t=PQ;
            type[i]=t; if (t==SLACK) slackIdx=i;
        }
        if (slackIdx<0){ _err="Nema slack čvora."; return false; }
        if (stop()) return false;
        setP(10);

        // Y-bus se gradi i pohranjuje u natID kompleksnu dense matricu (dense::CmplxMatrix).
        dense::CmplxMatrix Y;
        Y.reserve(n, n, nullptr, true);          // n×n, inicijalizovano na nule
        auto ym = Y.getManipulator();
        auto addY = [&](int i, int j, const cplx& v){ ym(i,j) = cplx(ym(i,j)) + v; };
        std::vector<std::set<int>> nbr(n);       // topologija (koji čvorovi su povezani)
        for (auto& br : _branch) {
            if (br.status != 1) continue;
            int fi=id2i[br.f], ti=id2i[br.t];
            cplx y = (br.r!=0||br.x!=0)? cplx(1,0)/cplx(br.r,br.x) : cplx(0,0);
            cplx bsh(0, br.b/2.0);
            double ratio = (br.ratio==0)?1.0:br.ratio;
            cplx a = std::polar(ratio, br.angle*M_PI/180.0);
            addY(fi,fi,(y+bsh)/(a*std::conj(a))); addY(ti,ti,(y+bsh));
            addY(fi,ti,-y/std::conj(a));           addY(ti,fi,-y/a);
            nbr[fi].insert(ti); nbr[ti].insert(fi);
        }
        for (int i=0;i<n;++i)
            if (_bus[i].Gs!=0||_bus[i].Bs!=0) addY(i,i,cplx(_bus[i].Gs,_bus[i].Bs)/_baseMVA);
        if (stop()) return false;
        setP(40);

        // injekcije
        std::vector<double> Pinj(n,0), Qinj(n,0);
        for (int i=0;i<n;++i){
            double Pg=0,Qg=0; auto it=gensAt.find(_bus[i].id);
            if (it!=gensAt.end()) for (auto* g:it->second){ Pg+=g->Pg; Qg+=g->Qg; }
            Pinj[i]=(Pg-_bus[i].Pd)/_baseMVA; Qinj[i]=(Qg-_bus[i].Qd)/_baseMVA;
        }
        int slackId=_bus[slackIdx].id;
        double Vslack=_bus[slackIdx].Vm;
        auto its=gensAt.find(slackId); if (its!=gensAt.end()) Vslack=its->second[0]->Vg;
        double dSlack=_bus[slackIdx].Va*M_PI/180.0;

        // ----- ispis -----
        std::ostringstream o;
        o << "Header:\n\tmaxIter=1000\n\treport=Solved\t//Solved/All/AllDetails\n\tmaxReps=-1\n\toutToTxt=false\nend\n";
        o << "//Generisao: Konvertor nelinearnog strujnog balansa (polarne koordinate) — Azra Babic 19029\n";
        o << "Model [type=NL domain=real eps=1e-6 name=\"Current balance in polar coordinates\"]:\n";

        o << "Vars [out=true]:\n";
        for (int i=0;i<n;++i){
            if (i==slackIdx) continue; int id=_bus[i].id;
            if (type[i]==PV) o << "\tδ_"<<id<<" = δ_"<<slackId<<"; Q_"<<id<<"_g = "<<num(Qinj[i])<<"\n";
            else             o << "\tδ_"<<id<<" = δ_"<<slackId<<"; V_"<<id<<" = V_"<<slackId<<"\n";
        }
        if (stop()) return false;
        setP(55);

        o << "Params:\n";
        o << "\tδ_"<<slackId<<" = "<<num(dSlack)<<" [out=true]; V_"<<slackId<<" = "<<num(Vslack)<<" [out=true]\n";
        for (int i=0;i<n;++i){
            int id=_bus[i].id;
            cplx yii = ym(i,i);                    // čitanje iz natID dense matrice
            o << "\tY_"<<id<<"_"<<id<<" = "<<num(std::abs(yii))
              << "; θ_"<<id<<"_"<<id<<" = "<<num(std::arg(yii))<<"\n";
            for (int j:nbr[i]){
                int jd=_bus[j].id; cplx y=ym(i,j);
                o << "\tY_"<<id<<"_"<<jd<<" = "<<num(std::abs(y))
                  << "; θ_"<<id<<"_"<<jd<<" = "<<num(std::arg(y))<<" // grana "<<id<<"-"<<jd<<"\n";
            }
        }
        for (int i=0;i<n;++i){
            if (i==slackIdx) continue; int id=_bus[i].id;
            if (type[i]==PV){
                auto it=gensAt.find(id); double vSet=(it!=gensAt.end())?it->second[0]->Vg:_bus[i].Vm;
                o << "\tV_"<<id<<" = "<<num(vSet)<<"\t// PV fiksni modul napona\n";
                o << "\tP_"<<id<<"_g = "<<num(Pinj[i])<<"\n";
            } else if (Pinj[i]!=0||Qinj[i]!=0){
                o << "\tP_"<<id<<" = "<<num(Pinj[i])<<"; Q_"<<id<<" = "<<num(Qinj[i])<<"\n";
            }
        }
        if (stop()) return false;
        setP(70);

        auto emitSum=[&](int i,bool c){
            int id=_bus[i].id; std::ostringstream s;
            s << "Y_"<<id<<"_"<<id<<"*V_"<<id<<"*"<<(c?"cos":"sin")<<"(θ_"<<id<<"_"<<id<<"+δ_"<<id<<")";
            for (int j:nbr[i]){ int jd=_bus[j].id;
                s << " + Y_"<<id<<"_"<<jd<<"*V_"<<jd<<"*"<<(c?"cos":"sin")<<"(θ_"<<id<<"_"<<jd<<"+δ_"<<jd<<")"; }
            return s.str();
        };
        o << "NLEs:\n";
        for (int i=0;i<n;++i){
            if (stop()) return false;
            if ((i & 0x3F) == 0) setP(70 + (29*i)/std::max(1,n)); // napredak po čvoru (bitno za velike mreže)
            if (i==slackIdx) continue; int id=_bus[i].id;
            if (type[i]==PV){
                o << "\t// čvor "<<id<<" - PV\n";
                o << "\t"<<emitSum(i,true) <<" = (P_"<<id<<"_g*cos(δ_"<<id<<") + Q_"<<id<<"_g*sin(δ_"<<id<<"))/V_"<<id<<"\n";
                o << "\t"<<emitSum(i,false)<<" = (P_"<<id<<"_g*sin(δ_"<<id<<") - Q_"<<id<<"_g*cos(δ_"<<id<<"))/V_"<<id<<"\n";
            } else {
                bool zero=(Pinj[i]==0&&Qinj[i]==0);
                o << "\t// čvor "<<id<<" - PQ"<<(zero?" (ZI)":"")<<"\n";
                if (zero){
                    o << "\t"<<emitSum(i,true) <<" = 0\n";
                    o << "\t"<<emitSum(i,false)<<" = 0\n";
                } else {
                    o << "\t"<<emitSum(i,true) <<" = (P_"<<id<<"*cos(δ_"<<id<<") + Q_"<<id<<"*sin(δ_"<<id<<"))/V_"<<id<<"\n";
                    o << "\t"<<emitSum(i,false)<<" = (P_"<<id<<"*sin(δ_"<<id<<") - Q_"<<id<<"*cos(δ_"<<id<<"))/V_"<<id<<"\n";
                }
            }
        }
        o << "end\n";
        outText = o.str();
        setP(100);
        return true;
    }
};

} // namespace cb
