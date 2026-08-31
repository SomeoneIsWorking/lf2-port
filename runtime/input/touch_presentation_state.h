#pragma once

namespace lf2::touch {

enum class LastInputDevice { touch, controller };

class PresentationState {
  public:
    void note_touch()
    {
        last_input_ = LastInputDevice::touch;
    }
    void note_controller()
    {
        last_input_ = LastInputDevice::controller;
    }

    bool shows_touch_controls(bool touch_controls_enabled) const
    {
        return touch_controls_enabled && last_input_ == LastInputDevice::touch;
    }

  private:
    LastInputDevice last_input_ = LastInputDevice::touch;
};

} // namespace lf2::touch
