// Board configuration adapted from ESPressway32 (MIT).
#pragma once
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_Parallel8 _bus;
    lgfx::Light_PWM _light;

public:
    LGFX() {
        auto bus_cfg = _bus.config();
        bus_cfg.freq_write = 10000000;
        bus_cfg.pin_wr = 8;
        bus_cfg.pin_rd = 9;
        bus_cfg.pin_rs = 7;
        bus_cfg.pin_d0 = 39;
        bus_cfg.pin_d1 = 40;
        bus_cfg.pin_d2 = 41;
        bus_cfg.pin_d3 = 42;
        bus_cfg.pin_d4 = 45;
        bus_cfg.pin_d5 = 46;
        bus_cfg.pin_d6 = 47;
        bus_cfg.pin_d7 = 48;
        _bus.config(bus_cfg);
        _panel.setBus(&_bus);

        auto panel_cfg = _panel.config();
        panel_cfg.pin_cs = 6;
        panel_cfg.pin_rst = 5;
        panel_cfg.pin_busy = -1;
        panel_cfg.panel_width = 170;
        panel_cfg.panel_height = 320;
        panel_cfg.memory_width = 240;
        panel_cfg.memory_height = 320;
        panel_cfg.offset_x = 35;
        panel_cfg.offset_y = 0;
        panel_cfg.offset_rotation = 0;
        panel_cfg.dummy_read_pixel = 8;
        panel_cfg.dummy_read_bits = 1;
        panel_cfg.readable = false;
        panel_cfg.rgb_order = false;
        panel_cfg.invert = true;
        panel_cfg.dlen_16bit = false;
        panel_cfg.bus_shared = false; // bus is exclusive to the panel; keeps DMA pushes async
        _panel.config(panel_cfg);

        auto light_cfg = _light.config();
        light_cfg.pin_bl = 38;
        light_cfg.invert = false;
        light_cfg.freq = 44100;
        light_cfg.pwm_channel = 0;
        _light.config(light_cfg);
        _panel.setLight(&_light);

        setPanel(&_panel);
    }
};
