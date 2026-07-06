// cbgen — headless pokretanje konvertora (test jezgre s natID matricama).
#include "../../CurrentBalancePlugin/src/Converter.h"
#include <iostream>
#include <fstream>
int main(int argc, const char* argv[]) {
    if (argc < 3) { std::cout << "cbgen <ulaz.m> <izlaz.dmodl>\n"; return 1; }
    cb::Converter c;
    if (!c.loadCase(argv[1])) { std::cout << "GRESKA: " << c.error() << "\n"; return 1; }
    std::string out;
    if (!c.convert(out, {}, nullptr)) { std::cout << "GRESKA: " << c.error() << "\n"; return 1; }
    std::ofstream f(argv[2]); f << out; f.close();
    std::cout << "OK: " << out.size() << " bajta, cvorova=" << c.busCount() << "\n";
    return 0;
}
