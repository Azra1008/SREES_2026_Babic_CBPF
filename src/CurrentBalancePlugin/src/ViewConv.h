// GUI prozor konvertora: izbor ulazne .m i izlazne .dmodl, dugme Konvertuj,
// progres-indikator i status. Konverzija ide u RADNOM threadu (std::thread),
// a progres se prikazuje na glavnoj (GUI) niti preko gui::thread::asyncExecInMainThread.
//
// Dinamički mod: kvačica "Dinamički model" otključa izbor SMETNJE (tip, čvor, vrijeme,
// iznos). Tada konvertor generiše DAE model + .vmodl (grafike koje dTwin crta).
#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/LineEdit.h>
#include <gui/NumericEdit.h>
#include <gui/CheckBox.h>
#include <gui/ComboBox.h>
#include <gui/ProgressIndicator.h>
#include <gui/GridLayout.h>
#include <gui/GridComposer.h>
#include <gui/HorizontalLayout.h>
#include <gui/FileDialog.h>
#include <gui/Thread.h>
#include <fo/FileOperations.h>
#include <sc/IPlugin.h>
#include <arch/MemoryOut.h>
#include <thread>
#include <atomic>
#include <memory>
#include <fstream>
#include "Converter.h"

void onClosedPluginWindow(); // u CBPlugin.cpp
void setPluginDynamic(bool); // u CBPlugin.cpp — postavlja tip modela (DAE/NL)

class ViewConv : public gui::View
{
protected:
    sc::IPlugin* _pIPlugin;
    sc::IPlugin::CallBack _onComplete;
    td::UINT4 _wndID;

    gui::Label _lblIn, _lblOut, _lblStatus;
    gui::LineEdit _editIn, _editOut, _editStatus;
    gui::Button _btnIn, _btnOut, _btnConvert;
    // --- kontrole dinamičkog moda (smetnja + grafici) ---
    gui::CheckBox _cbDynamic;
    gui::Label _lblDist, _lblBus, _lblMag, _lblT0, _lblT1;
    gui::ComboBox _cmbDist;
    gui::NumericEdit _neBus, _neMag, _neT0, _neT1;
    gui::ProgressIndicator _prog;
    gui::HorizontalLayout _hlButtons;
    gui::GridLayout _gl;

    td::String _outFileName;
    std::thread _worker;
    std::atomic<bool> _cancel{false};
    std::atomic<bool> _running{false};
    // Postavke smetnje očitane na GUI niti pri kliku (radni thread ih samo čita).
    bool _dynMode = false;
    dyn::Options _dynOpt;
    // Štit protiv use-after-free: async lambdas drže shared kopiju i provjere flag.
    std::shared_ptr<std::atomic<bool>> _alive = std::make_shared<std::atomic<bool>>(true);

    void joinWorker() { if (_worker.joinable()) _worker.join(); }

    // Radni thread: parsiranje + konverzija (statička ili dinamička) + upis datoteka.
    void workerMethod(std::string inPath, std::string outPath)
    {
        auto alive = _alive;
        cb::Converter conv;
        std::string text, vtext; bool ok = false; std::string err;
        bool dyn = _dynMode;

        auto progress = [this, alive](int pct){
            gui::thread::asyncExecInMainThread([this, alive, pct]{
                if (alive->load()) _prog.setValue(pct / 100.0);
            });
        };

        if (!conv.loadCase(inPath)) {
            err = conv.error();
        } else if (dyn) {
            ok = conv.convertDynamic(text, vtext, _dynOpt, progress, &_cancel);
            if (!ok) err = conv.error();
        } else {
            ok = conv.convert(text, progress, &_cancel);
            if (!ok) err = conv.error();
        }

        if (ok && !_cancel.load()) {
            std::ofstream f(outPath);
            if (f) { f << text; f.close(); }
            else   { ok = false; err = "Ne mogu kreirati izlaznu datoteku."; }
            if (ok && dyn) {
                // .vmodl (grafici) uz istu osnovu imena kao .dmodl
                std::string vpath = outPath;
                auto dot = vpath.find_last_of('.');
                if (dot != std::string::npos) vpath.resize(dot);
                vpath += ".vmodl";
                std::ofstream fv(vpath);
                if (fv) { fv << vtext; fv.close(); }
            }
        }

        if (_cancel.load()) return; // otkazano pri zatvaranju -> ne diraj GUI

        gui::thread::asyncExecInMainThread([this, alive, ok, err, text, vtext, dyn]{
            if (!alive->load()) return;
            finalizeOnMain(ok, err, text, vtext, dyn);
        });
    }

    void finalizeOnMain(bool ok, const std::string& err, const std::string& text,
                        const std::string& vtext, bool dyn)
    {
        if (ok) {
            _prog.setValue(1.0);
            // Digitalni model u arhivu.
            arch::MemoryOut* pa = _pIPlugin->getArchive(sc::IPlugin::ArchType::DigitalModel);
            if (pa) pa->put(text.c_str(), (td::UINT4)text.size());
            // Dinamički: vizuelni model (grafici) u arhivu + tip modela = DAE.
            if (dyn) {
                arch::MemoryOut* pv = _pIPlugin->getArchive(sc::IPlugin::ArchType::VisualModel);
                if (pv) pv->put(vtext.c_str(), (td::UINT4)vtext.size());
            }
            setPluginDynamic(dyn);
            _editStatus = dyn ? "Dinamicka konverzija uspjesna (model + grafici)."
                              : "Konverzija uspjesna.";
            _onComplete(_pIPlugin);
        } else {
            std::string msg = "GRESKA: " + err;
            _editStatus = msg.c_str();
        }
        _running = false;
    }

    // Očita postavke smetnje sa GUI niti (poziva se pri kliku, prije pokretanja threada).
    void readDynOptions()
    {
        _dynMode = _cbDynamic.isChecked();
        if (!_dynMode) return;
        _dynOpt = dyn::Options{};
        _dynOpt.distType = _cmbDist.getSelectedIndex();     // 0 = opterećenje, 1 = kratki spoj
        td::INT4 bus = 0; _neBus.getValue(bus);
        _dynOpt.distBusId = (bus > 0) ? (int)bus : -1;      // 0 => auto (prvi PQ s opterećenjem)
        float t0 = 0.5f, t1 = 6.f, mg = 0.f;
        _neT0.getValue(t0); _neT1.getValue(t1); _neMag.getValue(mg);
        _dynOpt.t0 = t0; _dynOpt.t1 = t1; _dynOpt.magnitude = mg;
    }

    void onConvertClicked()
    {
        if (_running.load()) return;            // već u toku
        joinWorker();                            // pridruži prethodni (ako je gotov)

        td::String in = _editIn.getText();
        td::String out = _editOut.getText();
        if (in.isEmpty())  { _editStatus = "GRESKA: prazna ulazna datoteka.";  return; }
        if (!fo::fileExists(in)) { _editStatus = "GRESKA: ulazna datoteka ne postoji."; return; }
        if (out.isEmpty()) { _editStatus = "GRESKA: prazna izlazna datoteka."; return; }

        readDynOptions();
        _outFileName = out;
        _cancel = false;
        _running = true;
        _prog.setValue(0.0);
        _editStatus = _dynMode ? "Dinamicka konverzija u toku..." : "Konverzija u toku...";

        _worker = std::thread(&ViewConv::workerMethod, this,
                              std::string(in.c_str()), std::string(out.c_str()));
    }

    void handleUserActions()
    {
        _btnIn.onClick([this]{
            gui::OpenFileDialog::show(this, "Odaberi MATPOWER .m", "*.m", _wndID + 1000,
                [this](gui::FileDialog* pDlg){
                    if (pDlg->getStatus() == gui::FileDialog::Status::OK) {
                        td::String fn = pDlg->getFileName();
                        if (!fn.isEmpty()) _editIn = fn;
                    }
                });
        });
        _btnOut.onClick([this]{
            gui::SaveFileDialog::show(this, "Sacuvaj .dmodl", "*.dmodl", _wndID + 2000,
                [this](gui::FileDialog* pDlg){
                    if (pDlg->getStatus() == gui::FileDialog::Status::OK) {
                        td::String fn = pDlg->getFileName();
                        if (!fn.isEmpty()) _editOut = fn;
                    }
                });
        });
        _btnConvert.onClick([this]{ onConvertClicked(); });
    }

    ViewConv() = delete;

public:
    ViewConv(sc::IPlugin* pIPlugin, const sc::IPlugin::CallBack& onComplete, td::UINT4 wndID)
        : _pIPlugin(pIPlugin)
        , _onComplete(onComplete)
        , _wndID(wndID)
        , _lblIn("Ulaz (.m):")
        , _lblOut("Izlaz (.dmodl):")
        , _lblStatus("Status:")
        , _btnIn("...")
        , _btnOut("...")
        , _btnConvert("Konvertuj")
        , _cbDynamic("Dinamicki model (smetnja + grafici)")
        , _lblDist("Tip smetnje:")
        , _lblBus("Cvor smetnje (0=auto):")
        , _lblMag("Iznos (0=ispad/def.):")
        , _lblT0("t pocetak [s]:")
        , _lblT1("t kraj [s]:")
        , _neBus(td::int4)
        , _neMag(td::real4, gui::LineEdit::Messages::DoNotSend, false, "Load: faktor (0=ispad); Short: dodana G [p.u.]", 3)
        , _neT0(td::real4, gui::LineEdit::Messages::DoNotSend, false, "Pocetak smetnje", 3)
        , _neT1(td::real4, gui::LineEdit::Messages::DoNotSend, false, "Kraj smetnje", 3)
        , _hlButtons(2)
        , _gl(10, 4)
    {
        _editStatus.setAsReadOnly();
        _cmbDist.addItem("Ispad/skok opterecenja");
        _cmbDist.addItem("Kratki spoj (pad napona)");
        _cmbDist.selectIndex(0);
        _cbDynamic.setChecked(false);
        _neBus.setValue(td::INT4(0));
        _neT0.setValue(0.5f);
        _neT1.setValue(6.0f);
        _neMag.setValue(0.0f);

        gui::GridComposer gc(_gl);
        gc.appendRow(_lblIn)  << _editIn  << _btnIn;
        gc.appendRow(_lblOut) << _editOut << _btnOut;
        gc.appendRow(_cbDynamic, 0);
        gc.appendRow(_lblDist); gc.appendCol(_cmbDist, 0);
        gc.appendRow(_lblBus) << _neBus << _lblMag << _neMag;
        gc.appendRow(_lblT0)  << _neT0  << _lblT1  << _neT1;
        gc.appendRow(_lblStatus); gc.appendCol(_editStatus, 0);
        gc.appendRow(_prog, 0);
        _hlButtons.appendSpacer() << _btnConvert;
        gc.appendRow(_hlButtons, 0);
        setLayout(&_gl);
        handleUserActions();
    }

    ~ViewConv() override { requestStopAndJoin(); }

    // Poziva WindowPlugin::onClose prije rušenja prozora.
    void requestStopAndJoin()
    {
        *_alive = false;
        _cancel = true;
        joinWorker();
    }

    td::String getOutFileName() const { return _outFileName; }
};
