// pfsolve — učita .dmodl NL model i riješi ga dTwin-ovim modSolver-om (bez GUI-ja).
// Služi za runtime validaciju konvertora strujnog balansa.
#include <sc/IModel.h>
#include <sc/SolutionOptions.h>
#include <cnt/SafeFullVector.h>
#include <mem/PointerReleaser.h>
#include <iostream>

int main(int argc, const char* argv[])
{
    if (argc < 2) { std::cout << "Upotreba: pfsolve <model.dmodl>\n"; return 1; }

    sc::ILog* pLog = sc::getConsoleLogger();
    sc::IRealStaticModel* pModel = sc::createRealStaticModel(sc::IStatic::Problem::NLE, pLog);
    if (!pModel) { std::cout << "GRESKA: createRealStaticModel\n"; return 1; }
    mem::PointerReleaser releaser(pModel);

    td::String inFn(argv[1]);
    if (!pModel->initFromFile(inFn)) { std::cout << "GRESKA: initFromFile (parsiranje .dmodl)\n"; return 1; }

    auto outIndices = pModel->getOutputSymbolIndices();
    auto outNames   = pModel->getOutputSymbolNames(outIndices);

    auto* solver = pModel->getSolverInterface();
    if (!solver) { std::cout << "GRESKA: getSolverInterface\n"; return 1; }

    sc::Solution sol = solver->solve();
    std::cout << "Solution status code = " << int(sol) << " (0 = OK)\n";
    if (sol != sc::Solution::OK) { std::cout << "SOLVE FAIL\n"; return 2; }

    cnt::SafeFullVector<double> vals;
    pModel->getOutputSymbolValues(outIndices, vals);
    std::cout << "=== Rjesenje (izlazni simboli) ===\n";
    for (size_t i = 0; i < outNames.size(); ++i)
        std::cout << "  " << outNames[i].c_str() << " = " << vals[i] << "\n";
    std::cout << "SOLVE OK\n";
    return 0;
}
