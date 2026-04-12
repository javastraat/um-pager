#pragma once
#include "hal_interface.h"

void hw_radio_begin();
int16_t hw_set_radio_params(radio_params_t &params);
void hw_get_radio_params(radio_params_t &params);
void hw_set_radio_default();
void hw_set_radio_listening();
void hw_set_radio_tx(radio_tx_params_t &params, bool continuous);
void hw_get_radio_rx(radio_rx_params_t &params);
