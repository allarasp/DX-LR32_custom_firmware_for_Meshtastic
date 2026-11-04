#include "test.h"
#include "sx126x.h"
#define TRY(expr) do { if((expr)!=SX126X_STATUS_OK) {result->failure=result->phase; goto cleanup;} } while(0)
static bool chip(unsigned expected,volatile uint32_t *raw) {
 sx126x_chip_status_t s;
 if(sx126x_get_status(0,&s)!=SX126X_STATUS_OK) return false;
 *raw=((unsigned)s.chip_mode<<4)|((unsigned)s.cmd_status<<1);
 return s.chip_mode==expected && s.cmd_status!=SX126X_CMD_STATUS_CMD_TIMEOUT &&
 s.cmd_status!=SX126X_CMD_STATUS_CMD_PROCESS_ERROR && s.cmd_status!=SX126X_CMD_STATUS_CMD_EXEC_FAILURE;
}
static bool errors(void) {
 sx126x_errors_mask_t e=0xffff;
 if(sx126x_get_device_errors(0,&e)!=SX126X_STATUS_OK)return false;
 result->errors=e;return e==0;
}
void run_test(void) {
 result->magic=0x44585246;result->mode=TEST_TX;result->phase=1;
 if(!pads(0) || !chip(SX126X_CHIP_MODE_STBY_RC,&result->initial) || !errors()) {result->failure=1;goto cleanup;}
 result->phase=2;
 TRY(sx126x_cal_img(0,0x6b,0x6f));
 TRY(sx126x_set_rx_tx_fallback_mode(0,SX126X_FALLBACK_STDBY_RC));
 TRY(sx126x_set_buffer_base_address(0,0,0));
 TRY(sx126x_set_dio_irq_params(0,0x03ff,0,0,0));
 TRY(sx126x_clear_irq_status(0,0x03ff));
 if(!errors()) {result->failure=2;goto cleanup;}
 if(!pads(1)) {result->failure=3;goto cleanup;}
#if TEST_TX
 result->phase=3;
 const sx126x_pa_cfg_params_t pa={2,2,0,1};
 const uint8_t packet[]={'D','X','T','1'};
 TRY(sx126x_cfg_tx_clamp(0));
 TRY(sx126x_set_pa_cfg(0,&pa));
 TRY(sx126x_set_tx_params(0,-9,SX126X_RAMP_200_US));
 TRY(sx126x_write_buffer(0,0,packet,sizeof(packet)));
 if(!errors() || !pads(2)) {result->failure=3;goto cleanup;}
 result->phase=4;
 TRY(sx126x_set_tx(0,100));
 if(!chip(SX126X_CHIP_MODE_TX,&result->operating)) {result->failure=4;goto cleanup;}
#else
 result->phase=4;
 TRY(sx126x_set_rx(0,500));
 if(!chip(SX126X_CHIP_MODE_RX,&result->operating)) {result->failure=4;goto cleanup;}
#endif
 result->phase=5;
 uint32_t start=now();
 for(unsigned guard=0;guard<2000;++guard) {
  sx126x_irq_mask_t irq=0;
  TRY(sx126x_get_irq_status(0,&irq));result->irq=irq;
#if !TEST_TX
  if(result->samples<16 && !(irq&(SX126X_IRQ_TIMEOUT|SX126X_IRQ_RX_DONE))) {
   int16_t rssi=0;TRY(sx126x_get_rssi_inst(0,&rssi));result->rssi[result->samples++]=rssi;
  }
#endif
  if(irq&(SX126X_IRQ_TX_DONE|SX126X_IRQ_RX_DONE|SX126X_IRQ_TIMEOUT)) {result->done=1;break;}
  if(now()-start>(TEST_TX?150u:650u))break;
  if(!delay_ms(TEST_TX?1u:10u)) {result->failure=5;break;}
 }
 if(!result->done || (TEST_TX && (!(result->irq&SX126X_IRQ_TX_DONE) || (result->irq&SX126X_IRQ_TIMEOUT)))) result->failure=5;
cleanup:
 /* A failed SPI cleanup gets a physical reset before releasing RF pins. */
 result->phase=6;
 if(sx126x_set_standby(0,SX126X_STANDBY_CFG_RC)!=SX126X_STATUS_OK) {
  if(!result->failure)result->failure=6;
  force_radio_reset();
 }
 if(!pads(1) && !result->failure)result->failure=6;
 if(!pads(0) && !result->failure)result->failure=6;
 if(!chip(SX126X_CHIP_MODE_STBY_RC,&result->final) || !errors()) {if(!result->failure)result->failure=6;}
 result->phase=7;
}
