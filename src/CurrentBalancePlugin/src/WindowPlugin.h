#pragma once
#include <gui/Window.h>
#include "ViewConv.h"

class WindowPlugin : public gui::Window
{
protected:
    ViewConv _view;
    sc::IPlugin::Cleaner _cleanPlugin;

    void onClose() override final
    {
        _view.requestStopAndJoin(); // zaustavi radni thread prije rušenja prozora
        _cleanPlugin();
        onClosedPluginWindow();
    }

public:
    WindowPlugin(gui::Window* parentWnd, sc::IPlugin* pIPlugin,
                 const sc::IPlugin::CallBack& onComplete, const sc::IPlugin::Cleaner& cleaner,
                 td::UINT4 wndID = 0)
        : gui::Window(gui::Size(820, 360), parentWnd, wndID)
        , _view(pIPlugin, onComplete, wndID)
        , _cleanPlugin(cleaner)
    {
        setTitle("Konvertor strujnog balansa (polarne koordinate)");
        setCentralView(&_view);
    }

    td::String getOutFileName() const { return _view.getOutFileName(); }
};
