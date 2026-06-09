#pragma once

namespace SKSEMenuFramework { namespace Model { class WindowInterface; } }

namespace Menu
{
    inline SKSEMenuFramework::Model::WindowInterface* stateOverlayWindow{ nullptr };

    void Register();
    void __stdcall RenderSettings();
    void __stdcall RenderStateOverlay();
}






