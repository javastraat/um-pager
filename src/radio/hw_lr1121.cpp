// Ported from t-lora-factory/factory/hw_lr1121.cpp
#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_LR1121

#include <LilyGoLib.h>
#include <Arduino.h>

static bool _high_freq = false;
static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;

#define LORA_ISR_FLAG _BV(0)

static void hw_radio_isr() {
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
        radioEvent,
        LORA_ISR_FLAG,
        &xHigherPriorityTaskWoken);
    if (xResult == pdPASS) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void hw_radio_begin() {
    radioEvent = xEventGroupCreate();
    // Radio register isr event
    radio.setPacketSentAction(hw_radio_isr);
}

int16_t hw_set_radio_params(radio_params_t &params) {
    int16_t state = 0;
    instance.lockSPI();
    instance.initLoRa();
    // Re-register TX ISR — initLoRa() resets all RadioLib callbacks
    radio.setPacketSentAction(hw_radio_isr);
    state = radio.setFrequency(params.freq);
    state = radio.setBandwidth(params.bandwidth);
    state = radio.setSpreadingFactor(params.sf);
    state = radio.setCodingRate(params.cr);
    state = radio.setSyncWord(params.syncWord);
    bool highFreq = false;
    if (params.freq >= 2400 && params.power > 13) {
        params.power = 13;
        highFreq = true;
    }
    state = radio.setOutputPower(params.power, highFreq);
    switch (params.mode) {
        case RADIO_DISABLE:
            state = radio.standby();
            break;
        case RADIO_TX:
            state = radio.startTransmit("");
            break;
        case RADIO_RX:
            state = radio.startReceive();
            break;
        case RADIO_CW:
            radio.standby();
            delay(5);
            radio.transmitDirect();
            break;
        default:
            break;
    }
    instance.unlockSPI();
    return state;
}

void hw_get_radio_params(radio_params_t &params) {
    params.bandwidth = 125.0;
    params.freq = RADIO_DEFAULT_FREQUENCY;
    params.cr = 5;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 12;
    params.power = 22;
    params.interval = 3000;
    params.syncWord = 0xCD;
}

void hw_set_radio_default() {
    radio_params_t params;
    hw_get_radio_params(params);
    hw_set_radio_params(params);
}

void hw_set_radio_listening() {
    instance.lockSPI();
    radio.startReceive();
    instance.unlockSPI();
}

void hw_set_radio_tx(radio_tx_params_t &params, bool continuous) {
    if (continuous) {
        EventBits_t eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
        if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
            params.state = -1;
            return;
        }
    }
    if (!params.data) {
        params.state = -1;
        return;
    }
    instance.lockSPI();
    radio.standby();   // LR1121 requires explicit standby before TX (same as coordinator)
    params.state = radio.startTransmit(params.data, params.length);
    instance.unlockSPI();
}

void hw_get_radio_rx(radio_rx_params_t &params) {
    EventBits_t eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        params.state = -1;
        return;
    }
    if (!params.data) {
        params.state = -1;
        return;
    }
    instance.lockSPI();
    params.length = radio.getPacketLength();
    params.state = radio.readData(params.data, params.length);
    params.rssi = radio.getRSSI();
    params.snr = radio.getSNR();
    radio.startReceive();
    instance.unlockSPI();
    if (last_send_millis + 200 > millis()) {
        params.length = 0;
        return;
    }
    params.data[params.length] = '\0';
}

#endif // ARDUINO_LILYGO_LORA_LR1121
