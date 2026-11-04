#!/usr/bin/env python3
from pathlib import Path
import shutil, subprocess, json
ROOT=Path(__file__).resolve().parents[1]
UP=ROOT/'.local/meshtastic-cyd'
SHA='54e0d8d0ab2ff56b3a9ce967e53f79e49af560fb'
assert subprocess.check_output(['git','-C',str(UP),'rev-parse','HEAD'],text=True).strip()==SHA
# Only modify the pinned checkout; all maintained source is outside it.
for f in ('variant.h','platformio.ini'):
 dest=UP/'variants/esp32/cyd-dxlr32'/f;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy(ROOT/'cyd/variant'/f,dest)
for f in ('DxlrUartInterface.cpp','DxlrUartInterface.h'):shutil.copy(ROOT/'cyd/radio'/f,UP/'src/mesh'/f)
shutil.copy(ROOT/'cyd/ui/CYDTouchCalibration.h',UP/'src/graphics/CYDTouchCalibration.h')
def original(path):return subprocess.check_output(['git','-C',str(UP),'show','HEAD:'+path],text=True)
s=original('src/mesh/RadioInterface.cpp')
s=s.replace('#include "RadioInterface.h"','#include "RadioInterface.h"\n#ifdef USE_DXLR_UART\n#include "DxlrUartInterface.h"\n#endif',1)
s=s.replace('    std::unique_ptr<RadioInterface> rIf = nullptr;','''#ifdef USE_DXLR_UART
    std::unique_ptr<RadioInterface> uart(new DxlrUartInterface());
    uart->init();radioType=SX1262_RADIO;return uart;
#endif
    std::unique_ptr<RadioInterface> rIf = nullptr;''',1)
(UP/'src/mesh/RadioInterface.cpp').write_text(s)
s=original('src/graphics/TFTDisplay.cpp')
s=s.replace('#include "main.h"','#include "main.h"\n#ifdef CYD_ST7789\n#include <Preferences.h>\n#include "CYDTouchCalibration.h"\n#include "input/InputBroker.h"\n#include "modules/CannedMessageModule.h"\n#endif',1)
s=s.replace('#elif defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER)','#elif defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(CYD_ST7789)',1)
s=s.replace('#if defined(ILI9341_DRIVER)\n    lgfx::Panel_ILI9341','#if defined(CYD_ST7789)\n    lgfx::Panel_ST7789 _panel_instance;\n#elif defined(ILI9341_DRIVER)\n    lgfx::Panel_ILI9341',1)
s=s.replace('#if defined(ILI9341_DRIVER)\n            cfg.spi_host','#if defined(ILI9341_DRIVER) || defined(CYD_ST7789)\n            cfg.spi_host',1)
# Only patch the shared ILI/ST7789 branch.
a=s.index('#elif defined(ILI9341_DRIVER) || defined(ILI9342_DRIVER) || defined(CYD_ST7789)')
b=s.index('#elif defined(ST7735_CS)',a)
part=s[a:b]
part=part.replace('    lgfx::Light_PWM _light_instance;','    lgfx::Light_PWM _light_instance;\n#ifdef CYD_ST7789\n    lgfx::Touch_XPT2046 _touch_instance;\n#endif',1)
part=part.replace('cfg.rgb_order = false;','cfg.rgb_order = true;',1)
part=part.replace('        setPanel(&_panel_instance);','''#ifdef CYD_ST7789
        {
            auto cfg=_touch_instance.config();
            cfg.spi_host=HSPI_HOST;cfg.freq=1000000;
            cfg.pin_sclk=TOUCH_SCLK;cfg.pin_mosi=TOUCH_MOSI;cfg.pin_miso=TOUCH_MISO;
            cfg.pin_cs=TOUCH_CS;cfg.pin_int=TOUCH_IRQ;
            cfg.x_min=200;cfg.x_max=3900;cfg.y_min=200;cfg.y_max=3900;
            cfg.bus_shared=false;cfg.offset_rotation=0;
            _touch_instance.config(cfg);_panel_instance.setTouch(&_touch_instance);
        }
#endif
        setPanel(&_panel_instance);''',1)
s=s[:a]+part+s[b:]
s=s.replace('    tft->fillScreen(TFT_BLACK);\n\n    if (this->linePixelBuffer',
'''#ifdef CYD_ST7789
    {
        Preferences prefs;uint16_t points[8]={0};
        prefs.begin("cydtouch",false);
        bool saved=prefs.getBytesLength("cal")==sizeof(points) && prefs.getBytes("cal",points,sizeof(points))==sizeof(points) && cydCalibrationValid(points);
        if(saved)tft->setTouchCalibrate(points);
        else {
            LOG_INFO("CYD touch calibration: press each colored corner cross until it moves");
            tft->setBrightness(180);tft->fillScreen(TFT_BLACK);
            tft->setTextColor(TFT_WHITE,TFT_BLACK);tft->setTextSize(1);
            tft->setCursor(35,90);tft->println("Touch calibration");
            tft->setCursor(35,110);tft->println("Press each corner cross");
            tft->calibrateTouch(points,(uint16_t)0xf81f,(uint16_t)0x0000,16);
            if(cydCalibrationValid(points)) {
                prefs.putBytes("cal",points,sizeof(points));
                LOG_INFO("CYD touch calibration saved");
            } else LOG_WARN("CYD calibration invalid; retry after reset");
        }
        prefs.end();
    }
#endif
    tft->fillScreen(TFT_BLACK);

    if (this->linePixelBuffer''',1)
s=s.replace('    setGeometry(GEOMETRY_RAWMODE, TFT_HEIGHT, TFT_WIDTH);',
 '#ifdef CYD_ST7789\n    setGeometry(GEOMETRY_RAWMODE,320,184);\n#else\n    setGeometry(GEOMETRY_RAWMODE,TFT_HEIGHT,TFT_WIDTH);\n#endif',1)
s=s.replace('    // Copy the Buffer to the Back Buffer',
'''#ifdef CYD_ST7789
    tft->fillRect(0,184,320,16,0x1082);
    tft->setTextColor((uint16_t)0xbdf7,(uint16_t)0x1082);
    tft->drawCenterString(config.lora.tx_enabled?"DX-LR32 | TX ON":"DX-LR32 | TX OFF",160,185,2);
    const char *labels[]={"Prev","Menu","Next","Chat"};
    for(int i=0;i<4;++i) {
        uint16_t color=i==3?0x03e8:0x1950;
        tft->fillRoundRect(i*80+2,202,76,36,5,color);
        tft->setTextColor((uint16_t)0xffff,color);
        tft->drawCenterString(labels[i],i*80+40,212,2);
    }
#endif
    // Copy the Buffer to the Back Buffer''',1)
s+='''
#ifdef CYD_ST7789
void cydToolbarTap(int16_t x,int16_t y) {
    int b=cydToolbarButton(x,y);if(b<0)return;
    if(b==3) {
        if(cannedMessageModule)cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST,0);
        return;
    }
    if(inputBroker) {
        InputEvent e={};e.source="cyd-toolbar";
        e.inputEvent=b==0?INPUT_BROKER_LEFT:b==1?INPUT_BROKER_SELECT:INPUT_BROKER_RIGHT;
        inputBroker->injectInputEvent(&e);
    }
}
#endif
'''
(UP/'src/graphics/TFTDisplay.cpp').write_text(s)
# Avoid initializing the unrelated default LoRa SPI bus on CYD display/touch pins.
s=original('src/main.cpp')
start=s.index('    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);')
s=s[:start]+'#ifndef USE_DXLR_UART\n'+s[start:]
end=s.index('    LOG_DEBUG("SPI.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)"',start)
end=s.index('\n',end)+1
s=s[:end]+'#endif\n'+s[end:]
s=s.replace('defined(ILI9342_DRIVER)', 'defined(ILI9342_DRIVER) || defined(CYD_ST7789)')
s=s.replace('    auto rIf = initLoRa();','#ifdef CYD_ST7789\n    if(screen)screen->setup();\n#endif\n    auto rIf = initLoRa();',1)
(UP/'src/main.cpp').write_text(s)
for path in ('src/graphics/Screen.cpp','src/mesh/NodeDB.cpp'):
    s=original(path).replace('defined(ILI9342_DRIVER)', 'defined(ILI9342_DRIVER) || defined(CYD_ST7789)')
    (UP/path).write_text(s)
s=original('src/input/InputBroker.cpp')
s=s.replace('        cardKbI2cImpl = new CardKbI2cImpl();\n        cardKbI2cImpl->init();',
 '#if !MESHTASTIC_EXCLUDE_I2C\n        cardKbI2cImpl = new CardKbI2cImpl();\n        cardKbI2cImpl->init();\n#endif',1)
(UP/'src/input/InputBroker.cpp').write_text(s)
s=original('src/modules/AdminModule.cpp')
s=s.replace('case meshtastic_AdminMessage_ota_request_tag: {\n#if defined(ARCH_ESP32)',
 'case meshtastic_AdminMessage_ota_request_tag: {\n#if defined(ARCH_ESP32) && !MESHTASTIC_EXCLUDE_WIFI',1)
(UP/'src/modules/AdminModule.cpp').write_text(s)
s=original('src/input/TouchScreenImpl1.cpp')
s=s.replace('#include "TouchScreenImpl1.h"','#include "TouchScreenImpl1.h"\n#ifdef CYD_ST7789\n#include "graphics/CYDTouchCalibration.h"\n#endif',1)
s=s.replace('void TouchScreenImpl1::onEvent(const TouchEvent &event)\n{',
 'void TouchScreenImpl1::onEvent(const TouchEvent &event)\n{\n#ifdef CYD_ST7789\n    if(event.y>=184) {if(event.touchEvent==TOUCH_ACTION_TAP)cydToolbarTap(event.x,event.y);return;}\n#endif',1)
(UP/'src/input/TouchScreenImpl1.cpp').write_text(s)
# Default to receive-only preparation. Future TX enabling is an explicit admin action.
prefs={'USERPREFS_CONFIG_LORA_REGION':'meshtastic_Config_LoRaConfig_RegionCode_EU_433',
 'USERPREFS_LORACONFIG_MODEM_PRESET':'meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST',
 'USERPREFS_CONFIG_OWNER_LONG_NAME':'DX-LR32 CYD experimental','USERPREFS_CONFIG_OWNER_SHORT_NAME':'DXCY',
 'USERPREFS_LORA_TX_DISABLED':'1','USERPREFS_TZ_STRING':'UTC0'}
defaults=json.loads('{'+','.join(line.strip().rstrip(',') for line in original('userPrefs.jsonc').splitlines() if line.strip().startswith('"'))+'}')
defaults.update(prefs)
(UP/'userPrefs.jsonc').write_text(json.dumps(defaults,indent=2)+'\n')
print('Prepared pinned Meshtastic CYD2USB/ST7789 + DXLR UART RX22/TX27; RF TX disabled')
