#pragma once

#include "RmlUi_Platform_SDL.h"

class RmlUiSystemInterface final : public SystemInterface_SDL {
  public:
    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override;
};

RmlUiSystemInterface &rmlui_system_interface();
