#pragma once
#include <stdint.h>
#include <stddef.h>

// --- Radio frequency constants ---
#define RADIO_DEFAULT_FREQUENCY  868.0

// --- Radio modes ---
enum RadioMode {
    RADIO_DISABLE,
    RADIO_TX,
    RADIO_RX,
    RADIO_CW,
};

// --- Radio parameter struct ---
typedef struct {
    bool isRunning;
    float freq;
    float bandwidth;
    uint16_t cr;
    uint8_t power;
    uint8_t sf;
    uint8_t mode;
    uint8_t syncWord;
    uint32_t interval;
} radio_params_t;

// --- Radio TX params ---
typedef struct {
    uint8_t *data;
    size_t  length;
    int state;
} radio_tx_params_t;

// --- Radio RX params ---
typedef struct {
    uint8_t *data;
    size_t  length;
    int16_t rssi;
    int16_t snr;
    int state;
} radio_rx_params_t;

// --- API prototypes (to be implemented in hw_lr1121.cpp) ---
int16_t hw_set_radio_params(radio_params_t &params);
void hw_get_radio_params(radio_params_t &params);
void hw_set_radio_default();
void hw_set_radio_listening();
void hw_set_radio_tx(radio_tx_params_t &params, bool continuous);
void hw_get_radio_rx(radio_rx_params_t &params);
