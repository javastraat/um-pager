// Ported from t-lora-factory/factory/hw_lr1121.cpp
#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_LR1121

#include <LilyGoLib.h>
#include <Arduino.h>

static bool _high_freq = false;
static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;

#define LORA_ISR_TX_FLAG _BV(0)
#define LORA_ISR_RX_FLAG _BV(1)

static void hw_radio_isr() {
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
        radioEvent,
        LORA_ISR_TX_FLAG,
        &xHigherPriorityTaskWoken);
    if (xResult == pdPASS) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

static void hw_radio_rx_isr() {
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
        radioEvent,
        LORA_ISR_RX_FLAG,
        &xHigherPriorityTaskWoken);
    if (xResult == pdPASS) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void hw_radio_begin() {
    radioEvent = xEventGroupCreate();
    // Register both TX and RX callbacks so polling sees packet arrivals too.
    radio.setPacketSentAction(hw_radio_isr);
    radio.setPacketReceivedAction(hw_radio_rx_isr);
    Serial.println("[LORA-HAL] hw_radio_begin");
}

int16_t hw_set_radio_params(radio_params_t &params) {
    int16_t state = 0;
    instance.lockSPI();
    instance.initLoRa();
    // initLoRa() resets RadioLib callbacks, so restore both ISR hooks.
    radio.setPacketSentAction(hw_radio_isr);
    radio.setPacketReceivedAction(hw_radio_rx_isr);
    state = radio.setFrequency(params.freq);
    state = radio.setBandwidth(params.bandwidth);
    state = radio.setSpreadingFactor(params.sf);
    state = radio.setCodingRate(params.cr);
    state = radio.setSyncWord(params.syncWord);
    // Align framing with coordinator RadioLib begin(...): explicit header, CRC on, preamble 8.
    state = radio.explicitHeader();
    state = radio.setCRC(true);
    state = radio.setPreambleLength(8);
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
    Serial.printf("[LORA-HAL] params f=%.3f bw=%.1f sf=%u cr=%u sw=0x%02X pwr=%u mode=%u st=%d\n",
                  params.freq, params.bandwidth, params.sf, params.cr,
                  params.syncWord, params.power, params.mode, (int)state);
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
    int16_t st = radio.startReceive();
    instance.unlockSPI();
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA-HAL] startReceive failed st=%d\n", (int)st);
    }
}

void hw_set_radio_tx(radio_tx_params_t &params, bool continuous) {
    if (continuous) {
        EventBits_t eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_TX_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
        if ((eventBits & LORA_ISR_TX_FLAG) != LORA_ISR_TX_FLAG) {
            params.state = -1;
            return;
        }
    }
    if (!params.data) {
        params.state = -1;
        return;
    }
    instance.lockSPI();
    int16_t stby = radio.standby();
    if (stby != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA-HAL] standby failed st=%d\n", (int)stby);
    }
    // Start TX and wait explicitly for TX IRQ so we know packet really finished.
    params.state = radio.startTransmit(params.data, params.length);
    instance.unlockSPI();

    if (params.state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA-HAL] transmit failed st=%d len=%u\n", params.state, (unsigned)params.length);
        return;
    }

    EventBits_t txDone = xEventGroupWaitBits(
        radioEvent,
        LORA_ISR_TX_FLAG,
        pdTRUE,
        pdTRUE,
        pdMS_TO_TICKS(5000)
    );
    if ((txDone & LORA_ISR_TX_FLAG) != LORA_ISR_TX_FLAG) {
        // Some LR1121 firmware/driver paths occasionally miss the TX-done callback.
        // Fall back to blocking transmit so discovery and heartbeat continue working.
        Serial.printf("[LORA-HAL] transmit timeout len=%u, fallback blocking TX\n", (unsigned)params.length);

        instance.lockSPI();
        (void)radio.finishTransmit();
        (void)radio.standby();
        int16_t fb = radio.transmit(params.data, params.length);
        instance.unlockSPI();

        if (fb != RADIOLIB_ERR_NONE) {
            params.state = -2;
            Serial.printf("[LORA-HAL] fallback transmit failed st=%d len=%u\n", (int)fb, (unsigned)params.length);
            return;
        }

        params.state = RADIOLIB_ERR_NONE;
        last_send_millis = millis();
        Serial.println("[LORA-HAL] fallback transmit ok");
        return;
    }

    instance.lockSPI();
    radio.finishTransmit();
    instance.unlockSPI();

    params.state = RADIOLIB_ERR_NONE;
    last_send_millis = millis();
}

void hw_get_radio_rx(radio_rx_params_t &params) {
    EventBits_t eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_RX_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if (!params.data) {
        params.state = -1;
        return;
    }

    bool irq_rx = ((eventBits & LORA_ISR_RX_FLAG) == LORA_ISR_RX_FLAG);

    instance.lockSPI();

    // Some LR1121 callback paths can miss RX IRQ notifications under load.
    // If no IRQ was seen, use packet-length polling as a fallback.
    size_t polledLen = radio.getPacketLength();
    if (!irq_rx && polledLen == 0) {
        instance.unlockSPI();
        params.state = -1;
        return;
    }

    params.length = polledLen;
    params.state = radio.readData(params.data, params.length);
    params.rssi = radio.getRSSI();
    params.snr = radio.getSNR();
    radio.startReceive();
    instance.unlockSPI();
    if (params.state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA-HAL] readData failed st=%d len=%u\n", params.state, (unsigned)params.length);
    }
    if (last_send_millis + 200 > millis()) {
        params.length = 0;
        return;
    }
    params.data[params.length] = '\0';
}

#endif // ARDUINO_LILYGO_LORA_LR1121
