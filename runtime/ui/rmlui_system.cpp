#include "rmlui_system.h"

#include "lf2_log.h"

bool RmlUiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String &message)
{
    Lf2LogLevel level = LF2_LOG_INFO;
    if (type == Rml::Log::LT_WARNING) level = LF2_LOG_WARNING;
    if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) level = LF2_LOG_ERROR;
    lf2_log_write(level, "rmlui", message.c_str());
    return true;
}

RmlUiSystemInterface &rmlui_system_interface()
{
    static RmlUiSystemInterface value;
    return value;
}
