// =====================================================================================
//  Konvertor nelinearnog strujnog balansa: polarne koordinate
//  Seminarski rad SREES — Azra Babić (19029)
//
//  Standalone verzija jezgre konvertora (bez natID-a, radi validaciju logike).
//  Ulaz:  MATPOWER .m datoteka (bus/gen/branch/baseMVA).
//  Izlaz: dTwin .dmodl  NL model s jednačinama STRUJNOG BALANSA u polarnim koordinatama.
//
//  Formulacija (po čvoru i):
//    proračunata struja:  I_i = Σ_k Y_ik · V_k ∠(θ_ik + δ_k)
//    zadana struja:       I_i^sp = (P_i − jQ_i)/V_i · ∠δ_i
//    realni dio:  Σ_k Y_ik·V_k·cos(θ_ik+δ_k) = (P_i·cosδ_i + Q_i·sinδ_i)/V_i
//    imag. dio:   Σ_k Y_ik·V_k·sin(θ_ik+δ_k) = (P_i·sinδ_i − Q_i·cosδ_i)/V_i
//
//    PQ čvor:  nepoznate δ_i, V_i        (P_i, Q_i poznati)        -> 2 jedn.
//    PV čvor:  nepoznate δ_i, Q_i_g      (V_i = V_sp fiksno param) -> 2 jedn.
//    Slack:    V, δ fiksni (param)       -> bez jednačina
// =====================================================================================
#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

using cplx = std::complex<double>;

static const int PQ = 1, PV = 2, SLACK = 3;

struct Bus    { int id, type; double Pd, Qd, Gs, Bs, Vm, Va; };
struct Gen    { int bus; double Pg, Qg, Qmax, Qmin, Vg; int status; };
struct Branch { int f, t; double r, x, b, ratio, angle; int status; };

struct Case {
    double baseMVA = 100.0;
    std::vector<Bus> bus;
    std::vector<Gen> gen;
    std::vector<Branch> branch;
};

// ---- Parsiranje MATPOWER .m -------------------------------------------------------
static std::string stripComment(const std::string& line) {
    auto p = line.find('%');
    return (p == std::string::npos) ? line : line.substr(0, p);
}

// pročita brojeve iz matrične sekcije "mpc.<name> = [ ... ];"
static std::vector<std::vector<double>> readMatrix(const std::vector<std::string>& lines,
                                                   size_t& i) {
    std::vector<std::vector<double>> rows;
    // pronađi '[' (može biti na istoj liniji kao '=')
    for (; i < lines.size(); ++i) {
        if (stripComment(lines[i]).find('[') != std::string::npos) break;
    }
    ++i; // prva linija podataka
    for (; i < lines.size(); ++i) {
        std::string s = stripComment(lines[i]);
        if (s.find(']') != std::string::npos) break;
        for (char& c : s) if (c == ';' || c == ',') c = ' ';
        std::istringstream iss(s);
        std::vector<double> row; double v;
        while (iss >> v) row.push_back(v);
        if (!row.empty()) rows.push_back(row);
    }
    return rows;
}

static bool parseMatpower(const std::string& fname, Case& c, std::string& err) {
    std::ifstream f(fname);
    if (!f) { err = "Ne mogu otvoriti ulaznu datoteku: " + fname; return false; }
    std::vector<std::string> lines; std::string ln;
    while (std::getline(f, ln)) lines.push_back(ln);

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string s = stripComment(lines[i]);
        if (s.find("mpc.baseMVA") != std::string::npos) {
            auto eq = s.find('=');
            if (eq != std::string::npos) c.baseMVA = std::stod(s.substr(eq + 1));
        } else if (s.find("mpc.bus") != std::string::npos && s.find('=') != std::string::npos) {
            auto m = readMatrix(lines, i);
            for (auto& r : m) if (r.size() >= 9)
                c.bus.push_back({int(r[0]), int(r[1]), r[2], r[3], r[4], r[5], r[7], r[8]});
        } else if (s.find("mpc.gen") != std::string::npos && s.find('=') != std::string::npos) {
            auto m = readMatrix(lines, i);
            for (auto& r : m) if (r.size() >= 8)
                c.gen.push_back({int(r[0]), r[1], r[2], r[3], r[4], r[5], int(r[7])});
        } else if (s.find("mpc.branch") != std::string::npos && s.find('=') != std::string::npos) {
            auto m = readMatrix(lines, i);
            for (auto& r : m) if (r.size() >= 11)
                c.branch.push_back({int(r[0]), int(r[1]), r[2], r[3], r[4], r[8], r[9], int(r[10])});
        }
    }
    if (c.bus.empty()) { err = "Nema bus podataka."; return false; }
    return true;
}

// ---- Pomoćno: formatiranje broja -------------------------------------------------
static std::string num(double v) {
    std::ostringstream o; o.precision(15); o << v; return o.str();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Upotreba: %s <ulaz.m> [izlaz.dmodl]\n", argv[0]);
        return 1;
    }
    std::string inFile = argv[1];
    std::string outFile = (argc >= 3) ? argv[2]
                          : inFile.substr(0, inFile.find_last_of('.')) + "_cb.dmodl";

    Case c; std::string err;
    if (!parseMatpower(inFile, c, err)) { std::fprintf(stderr, "GREŠKA: %s\n", err.c_str()); return 1; }

    const int n = (int)c.bus.size();
    std::map<int,int> id2idx;            // bus ID -> indeks
    for (int i = 0; i < n; ++i) id2idx[c.bus[i].id] = i;

    // ---- aktivni generatori po čvoru; postavi PV flag ----
    std::map<int,std::vector<const Gen*>> gensAt;
    for (auto& g : c.gen) if (g.status >= 1) gensAt[g.bus].push_back(&g);

    // ---- tipovi čvorova ----
    std::vector<int> type(n);
    int slackIdx = -1;
    for (int i = 0; i < n; ++i) {
        int t = c.bus[i].type;
        if (t == PV && gensAt.find(c.bus[i].id) == gensAt.end()) t = PQ; // PV bez gena -> PQ
        type[i] = t;
        if (t == SLACK) slackIdx = i;
    }
    if (slackIdx < 0) { std::fprintf(stderr, "GREŠKA: nema slack čvora.\n"); return 1; }

    // ---- Y-bus (sparse: dijagonala + mapa off-dijagonala) ----
    std::vector<cplx> Yii(n, cplx(0,0));
    std::map<std::pair<int,int>, cplx> Yij;
    std::vector<std::set<int>> nbr(n);   // susjedi (indeksi)

    for (auto& br : c.branch) {
        if (br.status != 1) continue;
        int fi = id2idx[br.f], ti = id2idx[br.t];
        cplx y = (br.r != 0 || br.x != 0) ? cplx(1,0)/cplx(br.r, br.x) : cplx(0,0);
        cplx bsh(0, br.b/2.0);
        double ratio = (br.ratio == 0) ? 1.0 : br.ratio;
        double ang = br.angle * M_PI/180.0;
        cplx a = std::polar(ratio, ang);                 // kompleksni odnos transformatora
        Yii[fi] += (y + bsh) / (a * std::conj(a));
        Yii[ti] += (y + bsh);
        Yij[{fi,ti}] += -y / std::conj(a);
        Yij[{ti,fi}] += -y / a;
        nbr[fi].insert(ti); nbr[ti].insert(fi);
    }
    // shuntovi na čvorovima
    for (int i = 0; i < n; ++i) {
        if (c.bus[i].Gs != 0 || c.bus[i].Bs != 0)
            Yii[i] += cplx(c.bus[i].Gs, c.bus[i].Bs) / c.baseMVA;
    }

    // ---- injekcije snaga (po jedinici) ----
    std::vector<double> Pinj(n,0), Qinj(n,0);
    for (int i = 0; i < n; ++i) {
        double Pg=0, Qg=0;
        auto it = gensAt.find(c.bus[i].id);
        if (it != gensAt.end()) for (auto* g : it->second) { Pg += g->Pg; Qg += g->Qg; }
        Pinj[i] = (Pg - c.bus[i].Pd) / c.baseMVA;
        Qinj[i] = (Qg - c.bus[i].Qd) / c.baseMVA;
    }

    // ---- slack napon i ugao ----
    int slackId = c.bus[slackIdx].id;
    double Vslack = c.bus[slackIdx].Vm;
    auto its = gensAt.find(slackId);
    if (its != gensAt.end()) Vslack = its->second[0]->Vg;
    double dSlack = c.bus[slackIdx].Va * M_PI/180.0;

    // ===================== ISPIS .dmodl =====================
    std::ofstream o(outFile);
    if (!o) { std::fprintf(stderr, "GREŠKA: ne mogu kreirati %s\n", outFile.c_str()); return 1; }

    o << "Header:\n\tmaxIter=1000\n\treport=Solved\t//Solved/All/AllDetails\n\tmaxReps=-1\n\toutToTxt=false\nend\n";
    o << "//Generisao: Konvertor nelinearnog strujnog balansa (polarne koordinate) — Azra Babic 19029\n";
    o << "Model [type=NL domain=real eps=1e-6 name=\"Current balance in polar coordinates\"]:\n";

    // --- Vars (sve sem slacka; PV: δ i Q_g; PQ: δ i V) ---
    o << "Vars [out=true]:\n";
    for (int i = 0; i < n; ++i) {
        if (i == slackIdx) continue;
        int id = c.bus[i].id;
        if (type[i] == PV)
            o << "\tδ_" << id << " = δ_" << slackId << "; Q_" << id << "_g = " << num(Qinj[i]) << "\n";
        else
            o << "\tδ_" << id << " = δ_" << slackId << "; V_" << id << " = V_" << slackId << "\n";
    }

    // --- Params ---
    o << "Params:\n";
    o << "\tδ_" << slackId << " = " << num(dSlack) << " [out=true]; V_" << slackId
      << " = " << num(Vslack) << " [out=true]\n";

    // Y/θ params: dijagonala + off-dijagonala (po čvoru, deterministički)
    for (int i = 0; i < n; ++i) {
        int id = c.bus[i].id;
        o << "\tY_" << id << "_" << id << " = " << num(std::abs(Yii[i]))
          << "; θ_" << id << "_" << id << " = " << num(std::arg(Yii[i])) << "\n";
        for (int j : nbr[i]) {
            int jd = c.bus[j].id;
            cplx y = Yij[{i,j}];
            o << "\tY_" << id << "_" << jd << " = " << num(std::abs(y))
              << "; θ_" << id << "_" << jd << " = " << num(std::arg(y))
              << " // grana " << id << "-" << jd << "\n";
        }
    }
    // injekcije
    for (int i = 0; i < n; ++i) {
        if (i == slackIdx) continue;
        int id = c.bus[i].id;
        if (type[i] == PV) {
            auto it = gensAt.find(id);
            double vSet = (it != gensAt.end()) ? it->second[0]->Vg : c.bus[i].Vm;
            o << "\tV_" << id << " = " << num(vSet) << "\t// PV fiksni modul napona\n";
            o << "\tP_" << id << "_g = " << num(Pinj[i]) << "\n";
        } else { // PQ
            if (Pinj[i] != 0 || Qinj[i] != 0)
                o << "\tP_" << id << " = " << num(Pinj[i]) << "; Q_" << id << " = " << num(Qinj[i]) << "\n";
        }
    }

    // --- NLEs: jednačine strujnog balansa ---
    o << "NLEs:\n";
    auto emitSum = [&](int i, bool cosTerm) {
        int id = c.bus[i].id;
        std::ostringstream s;
        s << "Y_" << id << "_" << id << "*V_" << id << "*"
          << (cosTerm ? "cos" : "sin") << "(θ_" << id << "_" << id << "+δ_" << id << ")";
        for (int j : nbr[i]) {
            int jd = c.bus[j].id;
            s << " + Y_" << id << "_" << jd << "*V_" << jd << "*"
              << (cosTerm ? "cos" : "sin") << "(θ_" << id << "_" << jd << "+δ_" << jd << ")";
        }
        return s.str();
    };

    for (int i = 0; i < n; ++i) {
        if (i == slackIdx) continue;
        int id = c.bus[i].id;
        if (type[i] == PV) {
            o << "\t// čvor " << id << " - PV\n";
            o << "\t" << emitSum(i,true)  << " = (P_" << id << "_g*cos(δ_" << id
              << ") + Q_" << id << "_g*sin(δ_" << id << "))/V_" << id << "\n";
            o << "\t" << emitSum(i,false) << " = (P_" << id << "_g*sin(δ_" << id
              << ") - Q_" << id << "_g*cos(δ_" << id << "))/V_" << id << "\n";
        } else { // PQ
            bool zero = (Pinj[i] == 0 && Qinj[i] == 0);
            o << "\t// čvor " << id << " - PQ" << (zero ? " (ZI)" : "") << "\n";
            if (zero) {
                o << "\t" << emitSum(i,true)  << " = 0\n";
                o << "\t" << emitSum(i,false) << " = 0\n";
            } else {
                o << "\t" << emitSum(i,true)  << " = (P_" << id << "*cos(δ_" << id
                  << ") + Q_" << id << "*sin(δ_" << id << "))/V_" << id << "\n";
                o << "\t" << emitSum(i,false) << " = (P_" << id << "*sin(δ_" << id
                  << ") - Q_" << id << "*cos(δ_" << id << "))/V_" << id << "\n";
            }
        }
    }
    o << "end\n";
    o.close();

    // ---- validacijski ispis na stderr ----
    std::fprintf(stderr, "OK: %d čvorova, %d generatora, %d grana, baseMVA=%g\n",
                 n, (int)c.gen.size(), (int)c.branch.size(), c.baseMVA);
    std::fprintf(stderr, "Tipovi: slack=čvor %d, V_slack=%g, δ_slack=%g rad\n", slackId, Vslack, dSlack);
    auto showY = [&](int a, int b){
        int ai=id2idx[a], bi=id2idx[b];
        cplx y = (a==b) ? Yii[ai] : Yij[{ai,bi}];
        std::fprintf(stderr, "  Y_%d_%d: |Y|=%.10g  θ=%.10g rad\n", a, b, std::abs(y), std::arg(y));
    };
    std::fprintf(stderr, "Provjera Y (uporedi sa case9.dmodl):\n");
    showY(1,1); showY(1,4); showY(4,4); showY(4,5); showY(5,5); showY(9,9);
    std::fprintf(stderr, "Izlaz zapisan u: %s\n", outFile.c_str());
    return 0;
}
