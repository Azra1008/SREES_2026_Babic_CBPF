// IPlugin omotač za Konvertor nelinearnog strujnog balansa (polarne koordinate).
// Seminarski SREES — Azra Babić (19029).
#include <compiler/Definitions.h>
#include <sc/IPlugin.h>
#include <cassert>
#include "WindowPlugin.h"

#ifdef MU_WINDOWS
	#ifdef PLUGIN_EXPORTS
	#define PLUGIN_API __declspec(dllexport)
	#else
	#define PLUGIN_API __declspec(dllimport)
	#endif
#else
	#ifdef PLUGIN_EXPORTS
	#define PLUGIN_API __attribute__((visibility("default")))
	#else
	#define PLUGIN_API
	#endif
#endif

class Plugin : public sc::IPlugin
{
    MemoryArchiveContainer _outArchives;
    WindowPlugin* _pWnd = nullptr;
    ModelType _modelType = ModelType::NL;   // NL (statički) ili DAE (dinamički mod)
public:
    void setModelType(ModelType t) { _modelType = t; }

    Plugin()
    {
        for (size_t i = 0; i < size_t(ArchType::NA); ++i)
            _outArchives[i] = nullptr;
    }

    void show(gui::Window* parentWnd, MemoryArchiveContainer& archives, td::UINT4 wndID,
              const sc::IPlugin::Cleaner& cleaner, const sc::IPlugin::CallBack& onComplete) override final
    {
        for (size_t i = 0; i < size_t(ArchType::NA); ++i)
            _outArchives[i] = archives[i];

        if (_pWnd)
            _pWnd->setFocus();
        else {
            _pWnd = new WindowPlugin(parentWnd, this, onComplete, cleaner, wndID);
            _pWnd->open();
        }
    }

    td::String getMenuName() const override final { return "Konvertor strujnog balansa (polarno)"; }

    arch::MemoryOut* getArchive(sc::IPlugin::ArchType type) override final
    {
        auto i = size_t(type);
        if (i >= getMaxSupportedArchiveParts()) return nullptr;
        return _outArchives[i];
    }

    MemoryArchiveContainer& getArchives() override final { return _outArchives; }
    td::String getOutFileName() const override final { assert(_pWnd); return _pWnd->getOutFileName(); }
    size_t getMaxSupportedArchiveParts() const override final { return size_t(ArchType::NA); }
    ModelType getModelType() const override final { return _modelType; } // NL (statički) / DAE (dinamički)

    void onClosedPluginWindow() { _pWnd = nullptr; }
};

static Plugin s_plugin;

void onClosedPluginWindow() { s_plugin.onClosedPluginWindow(); }

// Poziva ViewConv nakon konverzije: postavlja tip modela (dinamički = DAE, inače NL).
void setPluginDynamic(bool dynamic) {
    s_plugin.setModelType(dynamic ? sc::IPlugin::ModelType::DAE : sc::IPlugin::ModelType::NL);
}

extern "C"
{
    PLUGIN_API sc::IPlugin* getPluginInterface() { return &s_plugin; }
}
