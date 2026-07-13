// =====================================================================================
//  DynEmit.h — generisanje DINAMIČKOG (DAE) dTwin modela iz mreže + izabrane smetnje.
//  Čist std-C++ (bez natID), dijeli ga plugin (Converter.h) i standalone (cbconv.cpp).
//
//  Model prati profesorov primjer  examples/PowerSystem/Dynamics/case9_dyn.dmodl:
//   - detaljni generator (2-osni, 4. reda) + IEEE Type-1 AVR + prosti governor,
//   - mreža u polarnim koordinatama (isti Y-bus kao statički strujni balans),
//   - inicijalizacijski pod-model (NL) računa početno stanje iz tokova snaga,
//   - PostProc unosi smetnju (skok/ispad opterećenja ILI kratki spoj) u vremenu.
//  Uz .dmodl generiše i .vmodl (linePlot grafici = "izlaz" koji dTwin crta).
//
//  Seminarski SREES — Azra Babić (19029).
// =====================================================================================
#pragma once
#include <cmath>
#include <complex>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <sstream>

namespace dyn {

using cplx = std::complex<double>;
enum { PQ = 1, PV = 2, SLACK = 3 };
enum DistType { DIST_LOAD = 0, DIST_SHORT = 1 };

// Opcije smetnje koje korisnik bira u GUI-ju.
struct Options {
    int    distType   = DIST_LOAD; // DIST_LOAD (skok/ispad opterećenja) | DIST_SHORT (kratki spoj)
    int    distBusId  = -1;        // ID čvora na koji se primjenjuje smetnja
    double t0         = 0.5;       // početak smetnje [s]
    double t1         = 6.0;       // kraj smetnje [s]
    double magnitude  = 0.0;       // LOAD: faktor opterećenja (0=ispad, 2=dupliranje); SHORT: dodana poprečna G [p.u.] (0=>20)
    double startTime  = 0.0;
    double dTime      = 0.001;
    double endTime    = 20.0;
    std::string method = "RK2";
    std::string modelName = "Dynamics from current-balance converter";
};

// Sve što generatoru treba (napuni ga i plugin i standalone iz svog parsiranja).
struct Grid {
    int n = 0;
    std::vector<int>  id;        // bus ID po indeksu
    std::vector<int>  type;      // PQ/PV/SLACK po indeksu
    std::vector<cplx> Yii;       // dijagonala Y-busa
    std::map<std::pair<int,int>, cplx> Yij;  // off-dijagonala (indeks,indeks)
    std::vector<std::set<int>> nbr;          // susjedi (indeksi)
    std::vector<double> Pinj, Qinj;          // injekcije [p.u.]
    std::vector<double> Vset;                // PV/slack zadani modul napona
    int slackIdx = -1;
    double Vslack = 1.0, dSlack = 0.0;
    std::map<int,int> id2idx;
};

// ---- standardni dinamički parametri mašine (kao u case9_dyn.dmodl) ------------------
struct Machine {
    double H = 6.4, D = 28;
    double Xd = 0.146, Xd_p = 0.0608, Xq = 0.0969, Xq_p = 0.0969;
    double Tdo_p = 8.96, Tqo_p = 0.31, Tr = 0.02, T_gov = 0.3;
    double Ka = 250.0, Ta = 0.02, Ke = 1.0, Te = 0.8;
    double R = 0.05, Rs = 0.07, Se = 0.0, Kf = 0.063, Tf = 1.0;
};

inline std::string num(double v) { if (v == 0.0) v = 0.0; /* -0 -> 0 */ std::ostringstream o; o.precision(15); o << v; return o.str(); }

// =====================================================================================
class Emitter {
    const Grid& g;
    const Options& o;
    Machine m;
    std::vector<int> genIdx;   // indeksi generatorskih čvorova (SLACK + PV)
    int distBus_ = -1;         // efektivni čvor smetnje (razriješen, validan)

    int firstLoadBus() const {
        for (int i = 0; i < g.n; ++i) if (g.type[i] == PQ && (g.Pinj[i] != 0 || g.Qinj[i] != 0)) return g.id[i];
        for (int i = 0; i < g.n; ++i) if (g.type[i] == PQ) return g.id[i];
        return g.n ? g.id.front() : -1;
    }

    std::string vref(int idx, bool init) const {
        if (init && idx == g.slackIdx) return "V_" + std::to_string(g.id[idx]) + "_init";
        return "V_" + std::to_string(g.id[idx]);
    }
    std::string dref(int idx, bool init) const {
        if (init && idx == g.slackIdx) return "δ_" + std::to_string(g.id[idx]) + "_init";
        return "δ_" + std::to_string(g.id[idx]);
    }
    // Σ oblik snage:  ±Y_ii*V_i^2*trig(θ_ii) + V_i*(Σ_j Y_ij*V_j*trig(δ_i-θ_ij-δ_j))
    std::string powerSum(int i, bool cosT, bool init) const {
        int id = g.id[i];
        std::ostringstream s;
        s << (cosT ? "" : "-") << "Y_" << id << "_" << id << "*" << vref(i,init) << "^2*"
          << (cosT ? "cos" : "sin") << "(θ_" << id << "_" << id << ")";
        s << " + " << vref(i,init) << "*(";
        bool first = true;
        for (int j : g.nbr[i]) {
            int jd = g.id[j];
            if (!first) s << " + ";
            first = false;
            s << "Y_" << id << "_" << jd << "*" << vref(j,init) << "*" << (cosT ? "cos" : "sin")
              << "(" << dref(i,init) << "-θ_" << id << "_" << jd << "-" << dref(j,init) << ")";
        }
        s << ")";
        return s.str();
    }
    // Σ oblik struje (za ZI čvor):  ±Y_ii*V_i*trig(θ_ii) + Σ_j Y_ij*V_j*trig(δ_i-θ_ij-δ_j)
    std::string currentSum(int i, bool cosT, bool init) const {
        int id = g.id[i];
        std::ostringstream s;
        s << (cosT ? "" : "-") << "Y_" << id << "_" << id << "*" << vref(i,init) << "*"
          << (cosT ? "cos" : "sin") << "(θ_" << id << "_" << id << ")";
        for (int j : g.nbr[i]) {
            int jd = g.id[j];
            s << " + Y_" << id << "_" << jd << "*" << vref(j,init) << "*" << (cosT ? "cos" : "sin")
              << "(" << dref(i,init) << "-θ_" << id << "_" << jd << "-" << dref(j,init) << ")";
        }
        return s.str();
    }

public:
    Emitter(const Grid& grid, const Options& opt) : g(grid), o(opt) {
        for (int i = 0; i < g.n; ++i)
            if (g.type[i] == SLACK || g.type[i] == PV) genIdx.push_back(i);
        // Razriješi validan čvor smetnje (jednom, za .dmodl i .vmodl).
        distBus_ = o.distBusId;
        bool valid = (distBus_ >= 0 && g.id2idx.count(distBus_));
        if (o.distType == DIST_LOAD) {                 // opterećenje mora biti na PQ čvoru s teretom
            if (valid) { int bi = g.id2idx.at(distBus_); valid = (g.type[bi] == PQ && (g.Pinj[bi] != 0 || g.Qinj[bi] != 0)); }
            if (!valid) distBus_ = firstLoadBus();
        } else {                                        // kratki spoj: bilo koji čvor (default: čvor s teretom)
            if (!valid) distBus_ = firstLoadBus();
        }
    }

    // ---- glavni .dmodl -------------------------------------------------------------
    std::string buildDModl() const {
        std::ostringstream o_;
        // Header
        o_ << "Header:\n"
           << "\tmaxIter=100\n\treport=Solved\t//Solved/All/AllDetails\n\tmaxReps=-1\n\toutToTxt=false\n"
           << "\tstartTime=" << num(o.startTime) << "\t//početak vremenske simulacije\n"
           << "\tdTime=" << num(o.dTime) << "\t//integracijski korak\n"
           << "\tendTime=" << num(o.endTime) << "\t//kraj simulacije\n"
           << "end\n";
        o_ << "//Generisao: Konvertor strujnog balansa (dinamički mod) — Azra Babic 19029\n";
        o_ << "Model [type=DAE domain=real eps=1e-6 name=\"" << o.modelName
           << "\" method=" << o.method << "]:\n";

        // ---- Vars: svi čvorovi δ,V + dinamičke varijable po generatoru --------------
        o_ << "Vars [out=true]:\n";
        for (int i = 0; i < g.n; ++i)
            o_ << "\tδ_" << g.id[i] << " = 0.0; V_" << g.id[i] << " = 1.0\n";
        for (int gi : genIdx) {
            int b = g.id[gi];
            o_ << "\t// Dinamičke varijable generatora na čvoru " << b
               << (gi == g.slackIdx ? " (slack)" : " (PV)") << "\n";
            o_ << "\tδ_g" << b << "; ω_g" << b << " = ω_ref; Eq_p_g" << b << "; Ed_p_g" << b
               << "; Efd_g" << b << "; Rf_g" << b << "; VR_g" << b << "; P_gm_g" << b << "\n";
            o_ << "\tId_g" << b << "; Iq_g" << b << "; Pe_g" << b << "\n";
        }

        // ---- Params ----------------------------------------------------------------
        o_ << "Params:\n";
        int sId = g.id[g.slackIdx];
        o_ << "\tδ_" << sId << "_init = " << num(g.dSlack) << " [out=true]; V_" << sId
           << "_init = " << num(g.Vslack) << " [out=true]\n";
        // Y-bus (dijagonala + off-dijagonala) — isti Y kao statički strujni balans
        for (int i = 0; i < g.n; ++i) {
            int id = g.id[i];
            o_ << "\tY_" << id << "_" << id << " = " << num(std::abs(g.Yii[i]))
               << "; θ_" << id << "_" << id << " = " << num(std::arg(g.Yii[i])) << "\n";
            for (int j : g.nbr[i]) {
                int jd = g.id[j];
                cplx y = g.Yij.at({i, j});
                o_ << "\tY_" << id << "_" << jd << " = " << num(std::abs(y))
                   << "; θ_" << id << "_" << jd << " = " << num(std::arg(y)) << "\n";
            }
        }
        // injekcije
        for (int i = 0; i < g.n; ++i) {
            int id = g.id[i];
            if (g.type[i] == SLACK)
                o_ << "\tP_" << id << " = " << num(g.Pinj[i]) << "; Q_" << id << " = " << num(g.Qinj[i]) << "\n";
            else if (g.type[i] == PV)
                o_ << "\tP_" << id << "_g = " << num(g.Pinj[i]) << "; Q_" << id << "_g = " << num(g.Qinj[i]) << "\n";
            else if (g.Pinj[i] != 0 || g.Qinj[i] != 0)
                o_ << "\tP_" << id << " = " << num(g.Pinj[i]) << "; Q_" << id << " = " << num(g.Qinj[i]) << "\n";
        }
        // PV zadani napon
        for (int gi : genIdx) if (g.type[gi] == PV)
            o_ << "\tV_" << g.id[gi] << "_sp = " << num(g.Vset[gi]) << "\n";
        // globalni
        o_ << "\tf_s = 50; ω_ref = 1; ω_s = 2*pi*f_s\n";
        // Tm/Vref (računaju se u init) + mašinski parametri po generatoru
        for (int gi : genIdx) {
            int b = g.id[gi];
            o_ << "\tTm_g" << b << "; Vref_g" << b << "\n";
            o_ << "\tH_g" << b << " = " << num(m.H) << "; D_g" << b << " = " << num(m.D) << "\n";
            o_ << "\tXd_g" << b << " = " << num(m.Xd) << "; Xd_p_g" << b << " = " << num(m.Xd_p)
               << "; Xq_g" << b << " = " << num(m.Xq) << "; Xq_p_g" << b << " = " << num(m.Xq_p) << "\n";
            o_ << "\tTdo_p_g" << b << " = " << num(m.Tdo_p) << "; Tqo_p_g" << b << " = " << num(m.Tqo_p)
               << "; Tr_g" << b << " = " << num(m.Tr) << "; T_gov_g" << b << " = " << num(m.T_gov) << "\n";
            o_ << "\tKa_g" << b << " = " << num(m.Ka) << "; Ta_g" << b << " = " << num(m.Ta)
               << "; Ke_g" << b << " = " << num(m.Ke) << "; Te_g" << b << " = " << num(m.Te) << "\n";
            o_ << "\tR_g" << b << " = " << num(m.R) << "; Rs_g" << b << " = " << num(m.Rs)
               << "; Se_g" << b << " = " << num(m.Se) << "; Kf_g" << b << " = " << num(m.Kf)
               << "; Tf_g" << b << " = " << num(m.Tf) << "\n";
        }

        // ---- Inicijalizacijski pod-model (NL) --------------------------------------
        o_ << "SubModel [type=NL name=\"Initialization\" copyPars=-1 eps=1e-6]:\n";
        o_ << "Vars [out=true]:\n";
        for (int i = 0; i < g.n; ++i) {
            if (i == g.slackIdx) continue;
            o_ << "\tδ_" << g.id[i] << " = 0.0; V_" << g.id[i] << " = 1.0\n";
        }
        o_ << "Params:\n";
        for (int gi : genIdx) {
            int b = g.id[gi];
            o_ << "\tP_" << b << "_init; Q_" << b << "_init\n";
            o_ << "\tI_r" << b << "_init; I_i" << b << "_init; Eq_r" << b << "_init; Eq_i" << b << "_init\n";
            o_ << "\tI_d" << b << "_init; I_q" << b << "_init; V_d" << b << "_init; V_q" << b << "_init\n";
            o_ << "\tE_d" << b << "_p_init; E_q" << b << "_p_init; Ef_d" << b << "_init; VR" << b << "_init\n";
            o_ << "\tRf" << b << "_init; Vref" << b << "_init; Tm" << b << "_init\n";
        }
        o_ << "NLEs:\n";
        for (int i = 0; i < g.n; ++i) {
            if (i == g.slackIdx) continue;
            int id = g.id[i];
            if (g.type[i] == PV) {
                o_ << "\t// čvor " << id << " - PV\n";
                o_ << "\t" << powerSum(i, true, true) << " = P_" << id << "_g\n";
                o_ << "\tV_" << id << " = V_" << id << "_sp\n";
            } else {
                bool zero = (g.Pinj[i] == 0 && g.Qinj[i] == 0);
                o_ << "\t// čvor " << id << " - PQ" << (zero ? " (ZI)" : "") << "\n";
                if (zero) {
                    o_ << "\t" << currentSum(i, true, true) << " = 0\n";
                    o_ << "\t" << currentSum(i, false, true) << " = 0\n";
                } else {
                    o_ << "\t" << powerSum(i, true, true) << " = P_" << id << "\n";
                    o_ << "\t" << powerSum(i, false, true) << " = Q_" << id << "\n";
                }
            }
        }
        o_ << "PostProc:\n";
        for (int gi : genIdx) o_ << initSteps(gi);
        // prenos svih napona/uglova u glavni problem
        for (int i = 0; i < g.n; ++i) {
            int id = g.id[i];
            if (i == g.slackIdx)
                o_ << "\t@main.δ_" << id << " = δ_" << id << "_init; @main.V_" << id << " = V_" << id << "_init\n";
            else
                o_ << "\t@main.δ_" << id << " = δ_" << id << "; @main.V_" << id << " = V_" << id << "\n";
        }
        o_ << "end\n";

        // ---- ODEs ------------------------------------------------------------------
        o_ << "ODEs:\n";
        for (int gi : genIdx) {
            int b = g.id[gi];
            o_ << "\t// generator na čvoru " << b << "\n";
            o_ << "\tEq_p_g" << b << "' = (-Eq_p_g" << b << " - (Xd_g" << b << " - Xd_p_g" << b << ")*Id_g" << b
               << " + Efd_g" << b << ")/Tdo_p_g" << b << "\n";
            o_ << "\tEd_p_g" << b << "' = (-Ed_p_g" << b << " + (Xq_g" << b << " - Xq_p_g" << b << ")*Iq_g" << b
               << ")/Tqo_p_g" << b << "\n";
            o_ << "\tδ_g" << b << "' = (ω_g" << b << " - ω_ref) * ω_s\n";
            o_ << "\tω_g" << b << "' = (P_gm_g" << b << " - (Ed_p_g" << b << " * Id_g" << b << " + Eq_p_g" << b
               << " * Iq_g" << b << " + (Xq_p_g" << b << " - Xd_p_g" << b << ") * Id_g" << b << " * Iq_g" << b
               << ") - D_g" << b << "*(ω_g" << b << " - ω_ref)) * ω_s /(2*H_g" << b << ")\n";
            o_ << "\tEfd_g" << b << "' = (-(Ke_g" << b << " + Se_g" << b << ")*Efd_g" << b << " + VR_g" << b
               << ")/Te_g" << b << "\n";
            o_ << "\tRf_g" << b << "' = (-Rf_g" << b << " + (Kf_g" << b << "/Tf_g" << b << ")*Efd_g" << b
               << ")/Tf_g" << b << "\n";
            o_ << "\tVR_g" << b << "' = (-VR_g" << b << " + Ka_g" << b << "*Rf_g" << b << " - (Ka_g" << b
               << "*Kf_g" << b << "/Tf_g" << b << ")*Efd_g" << b << " + Ka_g" << b << "*(Vref_g" << b
               << " - V_" << b << "))/Ta_g" << b << "\n";
            o_ << "\tP_gm_g" << b << "' = (ω_ref - ω_g" << b << ") / (T_gov_g" << b << " * R_g" << b << " * ω_ref)\n";
        }

        // ---- NLEs: stator + Pe + mrežni strujni balans -----------------------------
        o_ << "NLEs:\n";
        o_ << "\t// statorske jednačine (d-q struje)\n";
        for (int gi : genIdx) {
            int b = g.id[gi];
            o_ << "\tId_g" << b << " = (Rs_g" << b << " * (Ed_p_g" << b << " - V_" << b << " * sin(δ_g" << b
               << " - δ_" << b << ")) + Xq_p_g" << b << " * (Eq_p_g" << b << " - V_" << b << " * cos(δ_g" << b
               << " - δ_" << b << "))) / (Rs_g" << b << "^2 + Xd_p_g" << b << " * Xq_p_g" << b << ")\n";
            o_ << "\tIq_g" << b << " = (-Xd_p_g" << b << " * (Ed_p_g" << b << " - V_" << b << " * sin(δ_g" << b
               << " - δ_" << b << ")) + Rs_g" << b << " * (Eq_p_g" << b << " - V_" << b << " * cos(δ_g" << b
               << " - δ_" << b << "))) / (Rs_g" << b << "^2 + Xd_p_g" << b << " * Xq_p_g" << b << ")\n";
        }
        o_ << "\t// električna snaga generatora\n";
        for (int gi : genIdx) {
            int b = g.id[gi];
            o_ << "\tPe_g" << b << " = Ed_p_g" << b << " * Id_g" << b << " + Eq_p_g" << b << " * Iq_g" << b
               << " + (Xq_p_g" << b << " - Xd_p_g" << b << ") * Id_g" << b << " * Iq_g" << b << "\n";
        }
        o_ << "\t// mrežni čvorovi (strujni balans u polarnim koordinatama)\n";
        for (int i = 0; i < g.n; ++i) {
            int id = g.id[i];
            if (g.type[i] == SLACK || g.type[i] == PV) {
                o_ << "\t// čvor " << id << " - generator (injekcija struje = struja u mrežu)\n";
                o_ << "\tV_" << id << "*(Id_g" << id << "*sin(δ_g" << id << " - δ_" << id << ") + Iq_g" << id
                   << "*cos(δ_g" << id << " - δ_" << id << ")) = " << powerSum(i, true, false) << "\n";
                o_ << "\tV_" << id << "*(Id_g" << id << "*cos(δ_g" << id << " - δ_" << id << ") - Iq_g" << id
                   << "*sin(δ_g" << id << " - δ_" << id << ")) = " << powerSum(i, false, false) << "\n";
            } else {
                bool zero = (g.Pinj[i] == 0 && g.Qinj[i] == 0);
                o_ << "\t// čvor " << id << " - PQ" << (zero ? " (ZI)" : "") << "\n";
                if (zero) {
                    o_ << "\t" << currentSum(i, true, false) << " = 0\n";
                    o_ << "\t" << currentSum(i, false, false) << " = 0\n";
                } else {
                    o_ << "\t" << powerSum(i, true, false) << " = P_" << id << "\n";
                    o_ << "\t" << powerSum(i, false, false) << " = Q_" << id << "\n";
                }
            }
        }

        // ---- PostProc: SMETNJA -----------------------------------------------------
        o_ << "PostProc:\n" << disturbance();
        o_ << "end\n";
        return o_.str();
    }

    // ---- 7-korak inicijalizacija jednog generatora ---------------------------------
    std::string initSteps(int gi) const {
        int b = g.id[gi];
        bool slack = (gi == g.slackIdx);
        std::ostringstream s;
        s << "\t// --- inicijalizacija generatora na čvoru " << b << " ---\n";
        if (slack) {
            s << "\tP_" << b << "_init = " << powerSum(gi, true, true) << "\n";
        } else {
            s << "\tP_" << b << "_init = P_" << b << "_g\n";
        }
        s << "\tQ_" << b << "_init = " << powerSum(gi, false, true) << "\n";
        std::string V = slack ? ("V_" + std::to_string(b) + "_init") : ("V_" + std::to_string(b));
        std::string D = slack ? ("δ_" + std::to_string(b) + "_init") : ("δ_" + std::to_string(b));
        s << "\tI_r" << b << "_init = (P_" << b << "_init*cos(" << D << ") + Q_" << b << "_init*sin(" << D << "))/" << V << "\n";
        s << "\tI_i" << b << "_init = (P_" << b << "_init*sin(" << D << ") - Q_" << b << "_init*cos(" << D << "))/" << V << "\n";
        s << "\tEq_r" << b << "_init = " << V << "*cos(" << D << ") + Rs_g" << b << "*I_r" << b << "_init - Xq_g" << b << "*I_i" << b << "_init\n";
        s << "\tEq_i" << b << "_init = " << V << "*sin(" << D << ") + Rs_g" << b << "*I_i" << b << "_init + Xq_g" << b << "*I_r" << b << "_init\n";
        s << "\t@main.δ_g" << b << " = atg2(Eq_r" << b << "_init, Eq_i" << b << "_init)\n";
        s << "\tI_d" << b << "_init = I_r" << b << "_init*sin(@main.δ_g" << b << ") - I_i" << b << "_init*cos(@main.δ_g" << b << ")\n";
        s << "\tI_q" << b << "_init = I_r" << b << "_init*cos(@main.δ_g" << b << ") + I_i" << b << "_init*sin(@main.δ_g" << b << ")\n";
        s << "\tV_d" << b << "_init = " << V << "*sin(@main.δ_g" << b << " - " << D << ")\n";
        s << "\tV_q" << b << "_init = " << V << "*cos(@main.δ_g" << b << " - " << D << ")\n";
        s << "\tE_d" << b << "_p_init = (Xq_g" << b << " - Xq_p_g" << b << ") * I_q" << b << "_init\n";
        s << "\tE_q" << b << "_p_init = V_q" << b << "_init + Rs_g" << b << " * I_q" << b << "_init + Xd_p_g" << b << " * I_d" << b << "_init\n";
        s << "\tEf_d" << b << "_init = E_q" << b << "_p_init + (Xd_g" << b << " - Xd_p_g" << b << ")*I_d" << b << "_init\n";
        s << "\tVR" << b << "_init = (Ke_g" << b << " + Se_g" << b << ") * Ef_d" << b << "_init\n";
        s << "\tRf" << b << "_init = (Kf_g" << b << " / Tf_g" << b << ") * Ef_d" << b << "_init\n";
        s << "\tVref" << b << "_init = " << V << " + (VR" << b << "_init / Ka_g" << b << ")\n";
        s << "\t@main.ω_g" << b << " = ω_ref\n";
        s << "\t@main.Ed_p_g" << b << " = E_d" << b << "_p_init\n";
        s << "\t@main.Eq_p_g" << b << " = E_q" << b << "_p_init\n";
        s << "\t@main.VR_g" << b << " = VR" << b << "_init\n";
        s << "\t@main.Rf_g" << b << " = Rf" << b << "_init\n";
        s << "\t@main.Id_g" << b << " = I_d" << b << "_init\n";
        s << "\t@main.Iq_g" << b << " = I_q" << b << "_init\n";
        s << "\t@main.Efd_g" << b << " = Ef_d" << b << "_init\n";
        s << "\t@main.Vref_g" << b << " = Vref" << b << "_init\n";
        s << "\tTm" << b << "_init = @main.Ed_p_g" << b << "*I_d" << b << "_init + @main.Eq_p_g" << b << "*I_q" << b
          << "_init + (Xq_p_g" << b << " - Xd_p_g" << b << ")*I_d" << b << "_init*I_q" << b << "_init\n";
        s << "\t@main.Pe_g" << b << " = Tm" << b << "_init\n";
        s << "\t@main.P_gm_g" << b << " = Tm" << b << "_init\n";
        s << "\t@main.Tm_g" << b << " = Tm" << b << "_init\n";
        if (slack)
            s << "\t@main.Q_" << b << " = Q_" << b << "_init; @main.P_" << b << " = P_" << b << "_init\n";
        else
            s << "\t@main.Q_" << b << "_g = Q_" << b << "_init; @main.P_" << b << "_g = P_" << b << "_init\n";
        return s.str();
    }

    // ---- SMETNJA (PostProc glavnog modela) -----------------------------------------
    std::string disturbance() const {
        int b = distBus_;
        int bi = g.id2idx.count(b) ? g.id2idx.at(b) : -1;
        std::ostringstream s;
        if (o.distType == DIST_LOAD) {
            double origP = (bi >= 0) ? g.Pinj[bi] : 0.0;
            double origQ = (bi >= 0) ? g.Qinj[bi] : 0.0;
            double f = o.magnitude;                 // 0 => ispad; inače faktor
            double fP = f * origP, fQ = f * origQ;
            s << "\t// SMETNJA: " << (f == 0 ? "ispad" : "skok") << " opterećenja na čvoru " << b
              << " od t=" << num(o.t0) << "s do t=" << num(o.t1) << "s\n";
            s << "\tif t >= " << num(o.t0) << ":\n";
            s << "\t\tif t < " << num(o.t1) << ":\n";
            s << "\t\t\tP_" << b << " = " << num(fP) << "; Q_" << b << " = " << num(fQ) << "\n";
            s << "\t\telse:\n";
            s << "\t\t\tP_" << b << " = " << num(origP) << "; Q_" << b << " = " << num(origQ) << "\n";
            s << "\t\tend\n\tend\n";
        } else { // DIST_SHORT — kratki spoj (dodatna poprečna admitansa na čvoru)
            double Gf = (o.magnitude > 0) ? o.magnitude : 5.0;    // dodana G [p.u.] (default umjeren zbog konvergencije)
            cplx pre = (bi >= 0) ? g.Yii[bi] : cplx(0, 0);
            cplx flt = pre + cplx(Gf, 0);
            s << "\t// SMETNJA: kratki spoj (pad napona) na čvoru " << b
              << " od t=" << num(o.t0) << "s do t=" << num(o.t1) << "s (dodana G=" << num(Gf) << " p.u.)\n";
            s << "\tif t >= " << num(o.t0) << ":\n";
            s << "\t\tif t < " << num(o.t1) << ":\n";
            s << "\t\t\tY_" << b << "_" << b << " = " << num(std::abs(flt)) << "; θ_" << b << "_" << b << " = " << num(std::arg(flt)) << "\n";
            s << "\t\telse:\n";
            s << "\t\t\tY_" << b << "_" << b << " = " << num(std::abs(pre)) << "; θ_" << b << "_" << b << " = " << num(std::arg(pre)) << "\n";
            s << "\t\tend\n\tend\n";
        }
        return s.str();
    }

    // ---- .vmodl (grafici = izlaz) --------------------------------------------------
    std::string buildVModl() const {
        const char* col[] = {"cyan","orange","magenta","green","blue","red","yellow","white"};
        int nc = 8;
        std::ostringstream s;
        s << "Header:\n\tnewTab = false\nend\n";
        s << "Model [name=\"Dinamički odziv — strujni balans (Azra Babic 19029)\"]:\n";
        s << "\tPlots:\n";
        // 1) frekvencija (brzina rotora)
        s << "\t\tlinePlot [xLabel=\"Vrijeme [s]\" yLabel=\"Brzina ω [p.u.]\" name=\"Frekvencija (brzina rotora)\"]:\n";
        s << "\t\t\t@x << t\n";
        { int k = 0; for (int gi : genIdx) { int b = g.id[gi];
            s << "\t\t\t@y << ω_g" << b << " [colorD=" << col[k%nc] << " width=2 name=\"ω_g" << b << "\"]\n"; ++k; } }
        s << "\t\t\t@y << ω_ref [colorD=white width=1 pattern=\"dot\" name=\"ω_ref\"]\n";
        s << "\t\tend\n";
        // 2) uglovi rotora
        s << "\t\tlinePlot [xLabel=\"Vrijeme [s]\" yLabel=\"Ugao δ [rad]\" name=\"Uglovi rotora\"]:\n";
        s << "\t\t\t@x << t\n";
        { int k = 0; for (int gi : genIdx) { int b = g.id[gi];
            s << "\t\t\t@y << δ_g" << b << " [colorD=" << col[k%nc] << " width=2 name=\"δ_g" << b << "\"]\n"; ++k; } }
        s << "\t\tend\n";
        // 3) naponi (generatorski čvorovi + čvor smetnje)
        s << "\t\tlinePlot [xLabel=\"Vrijeme [s]\" yLabel=\"Napon [p.u.]\" name=\"Naponi čvorova\"]:\n";
        s << "\t\t\t@x << t\n";
        { std::set<int> shown; int k = 0;
          for (int gi : genIdx) { int b = g.id[gi]; shown.insert(b);
            s << "\t\t\t@y << V_" << b << " [colorD=" << col[k%nc] << " width=2 name=\"V_" << b << "\"]\n"; ++k; }
          if (distBus_ >= 0 && !shown.count(distBus_)) {
            s << "\t\t\t@y << V_" << distBus_ << " [colorD=" << col[k%nc] << " width=2 pattern=\"dot\" name=\"V_"
              << distBus_ << " (smetnja)\"]\n"; }
        }
        s << "\t\tend\n";
        // 4) snage
        s << "\t\tlinePlot [xLabel=\"Vrijeme [s]\" yLabel=\"Snaga [p.u.]\" name=\"Mehanička i električna snaga\"]:\n";
        s << "\t\t\t@x << t\n";
        { int k = 0; for (int gi : genIdx) { int b = g.id[gi];
            s << "\t\t\t@y << P_gm_g" << b << " [colorD=" << col[k%nc] << " width=2 name=\"P_gm_g" << b << "\"]\n";
            s << "\t\t\t@y << Pe_g" << b << " [colorD=" << col[k%nc] << " width=2 pattern=\"dot\" name=\"Pe_g" << b << "\"]\n"; ++k; } }
        s << "\t\tend\n";
        s << "end\n";
        return s.str();
    }
};

// Javne funkcije.
inline std::string buildDModl(const Grid& g, const Options& o) { return Emitter(g, o).buildDModl(); }
inline std::string buildVModl(const Grid& g, const Options& o) { return Emitter(g, o).buildVModl(); }

} // namespace dyn
