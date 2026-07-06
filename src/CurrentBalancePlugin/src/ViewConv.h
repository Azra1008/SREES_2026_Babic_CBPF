// GUI prozor konvertora: izbor ulazne .m i izlazne .dmodl, dugme Konvertuj,
// progres-indikator i status. Konverzija ide u RADNOM threadu (std::thread),
// a progres se prikazuje na glavnoj (GUI) niti preko gui::thread::asyncExecInMainThread.
#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/Button.h>
#include <gui/LineEdit.h>
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

class ViewConv : public gui::View
{
protected:
    sc::IPlugin* _pIPlugin;
    sc::IPlugin::CallBack _onComplete;
    td::UINT4 _wndID;

    gui::Label _lblIn, _lblOut, _lblStatus;
    gui::LineEdit _editIn, _editOut, _editStatus;
    gui::Button _btnIn, _btnOut, _btnConvert;
    gui::ProgressIndicator _prog;
    gui::HorizontalLayout _hlButtons;
    gui::GridLayout _gl;

    td::String _outFileName;
    std::thread _worker;
    std::atomic<bool> _cancel{false};
    std::atomic<bool> _running{false};
    // Štit protiv use-after-free: async lambdas drže shared kopiju i provjere flag.
    std::shared_ptr<std::atomic<bool>> _alive = std::make_shared<std::atomic<bool>>(true);

    void joinWorker() { if (_worker.joinable()) _worker.join(); }

    // Radni thread: parsiranje + konverzija + upis datoteke.
    void workerMethod(std::string inPath, std::string outPath)
    {
        auto alive = _alive;
        cb::Converter conv;
        std::string text; bool ok = false; std::string err;

        if (!conv.loadCase(inPath)) {
            err = conv.error();
        } else {
            ok = conv.convert(text,
                [this, alive](int pct){
                    gui::thread::asyncExecInMainThread([this, alive, pct]{
                        if (alive->load()) _prog.setValue(pct / 100.0);
                    });
                },
                &_cancel);
            if (!ok) err = conv.error();
        }

        if (ok && !_cancel.load()) {
            std::ofstream f(outPath);
            if (f) { f << text; f.close(); }
            else   { ok = false; err = "Ne mogu kreirati izlaznu datoteku."; }
        }

        if (_cancel.load()) return; // otkazano pri zatvaranju -> ne diraj GUI

        // Finalizacija na glavnoj niti (FIFO: nakon svih progress poruka).
        gui::thread::asyncExecInMainThread([this, alive, ok, err, text]{
            if (!alive->load()) return;
            finalizeOnMain(ok, err, text);
        });
    }

    void finalizeOnMain(bool ok, const std::string& err, const std::string& text)
    {
        if (ok) {
            _prog.setValue(1.0);
            // Napuni digitalni model u arhivu (ako je host obezbijedio).
            arch::MemoryOut* pa = _pIPlugin->getArchive(sc::IPlugin::ArchType::DigitalModel);
            if (pa) pa->put(text.c_str(), (td::UINT4)text.size());
            _editStatus = "Konverzija uspjesna.";
            _onComplete(_pIPlugin);
        } else {
            std::string msg = "GRESKA: " + err;
            _editStatus = msg.c_str();
        }
        _running = false;
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

        _outFileName = out;
        _cancel = false;
        _running = true;
        _prog.setValue(0.0);
        _editStatus = "Konverzija u toku...";

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
        , _hlButtons(2)
        , _gl(5, 3)
    {
        _editStatus.setAsReadOnly();
        gui::GridComposer gc(_gl);
        gc.appendRow(_lblIn)  << _editIn  << _btnIn;
        gc.appendRow(_lblOut) << _editOut << _btnOut;
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
