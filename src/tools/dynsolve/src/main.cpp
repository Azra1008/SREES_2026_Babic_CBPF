// dynsolve — učita DINAMIČKI (DAE) .dmodl i odsimulira ga dTwin-ovim modSolver-om (bez GUI-ja).
// reset() riješi inicijalizaciju, step() pomjera rješenje kroz vrijeme.
// Runtime validacija dinamičkog konvertora strujnog balansa.
#include <sc/IModel.h>
#include <sc/SolutionOptions.h>
#include <cnt/SafeFullVector.h>
#include <mem/PointerReleaser.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cmath>

int main(int argc, const char* argv[])
{
    if (argc < 2) { std::cout << "Upotreba: dynsolve <model.dmodl> [endTime=20] [nSamples=20]\n"; return 1; }
    double endTime  = (argc > 2) ? std::atof(argv[2]) : 20.0;
    int    nSamples = (argc > 3) ? std::atoi(argv[3]) : 20;

    sc::ILog* pLog = sc::getConsoleLogger();
    sc::IRealDynamicModel* pModel = sc::createRealDynamicModel(sc::IDynamic::Problem::DAE, pLog);
    if (!pModel) { std::cout << "GRESKA: createRealDynamicModel\n"; return 1; }
    mem::PointerReleaser releaser(pModel);

    td::String inFn(argv[1]);
    if (!pModel->initFromFile(inFn)) { std::cout << "GRESKA: initFromFile (parsiranje .dmodl)\n"; return 1; }

    auto outIdx   = pModel->getOutputSymbolIndices();
    auto outNames = pModel->getOutputSymbolNames(outIdx);

    // Simboli koje pratimo (ako postoje u modelu).
    std::vector<std::string> want = {"omega_g","ω_g1","ω_g2","ω_g3","V_5","δ_g1","δ_g2","δ_g3","V_1","V_2"};
    std::vector<int> trackPos; std::vector<std::string> trackName;
    for (size_t i = 0; i < outNames.size(); ++i) {
        std::string nm = outNames[i].c_str();
        for (auto& w : want) if (nm == w) { trackPos.push_back((int)i); trackName.push_back(nm); }
    }

    auto* solver = pModel->getSolverInterface();
    if (!solver) { std::cout << "GRESKA: getSolverInterface\n"; return 1; }

    if (!solver->reset(0)) { std::cout << "GRESKA: reset() — inicijalizacija NIJE riješena\n"; return 2; }
    std::cout << "reset() OK — inicijalizacija riješena.\n";

    double dT = solver->getStepSize();
    if (dT <= 0) dT = 0.001;
    int totalSteps = (int)std::llround(endTime / dT);
    int every = (nSamples > 0) ? std::max(1, totalSteps / nSamples) : totalSteps;
    std::cout << "dT=" << dT << ", koraka=" << totalSteps << " (do t=" << endTime << "s)\n";

    // zaglavlje tabele
    std::cout << "\n     t[s]";
    for (auto& nm : trackName) std::cout << "  " << nm;
    std::cout << "\n";

    auto printRow = [&](double t){
        cnt::SafeFullVector<double> vals;
        pModel->getOutputSymbolValues(outIdx, vals);
        std::cout.setf(std::ios::fixed); std::cout.precision(4);
        std::cout << "  " << t;
        for (int p : trackPos) std::cout << "  " << vals[p];
        std::cout << "\n";
    };

    printRow(0.0);
    double wmin = 1e9, wmax = -1e9; bool haveW = false;
    double t = 0.0;
    for (int s = 0; s < totalSteps; ++s) {
        sc::Solution sol = solver->step(1);
        t += dT;
        if (sol != sc::Solution::OK) {
            std::cout << "STEP FAIL @ korak " << s << " (t=" << t << "s), kod=" << int(sol) << "\n";
            return 3;
        }
        // prati raspon frekvencije radi provjere stabilnosti
        cnt::SafeFullVector<double> vals;
        pModel->getOutputSymbolValues(outIdx, vals);
        for (size_t i = 0; i < trackName.size(); ++i)
            if (trackName[i].rfind("ω_g", 0) == 0) { haveW = true; double w = vals[trackPos[i]];
                if (w < wmin) wmin = w; if (w > wmax) wmax = w; }
        if ((s + 1) % every == 0 || s == totalSteps - 1) printRow(t);
    }
    std::cout << "\nSIMULACIJA OK (" << totalSteps << " koraka do t=" << t << "s)\n";
    if (haveW) std::cout << "Raspon brzine ω: [" << wmin << ", " << wmax << "] p.u. "
                         << (std::fabs(wmin-1) < 0.2 && std::fabs(wmax-1) < 0.2 ? "(stabilno)" : "(provjeri)") << "\n";
    return 0;
}
