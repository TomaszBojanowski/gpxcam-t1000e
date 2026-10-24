#include "usb_power.h"

#include <Arduino.h>
#include <nrf_clock.h>
#include <nrf_sdm.h>
#include <nrf_soc.h>

#include "log.h"

// The original handler (dcd_nrf5x.c) and this guard, joined by the linker's --wrap.
extern "C" void __real_tusb_hal_nrf_power_event(uint32_t event);
extern "C" void __wrap_tusb_hal_nrf_power_event(uint32_t event);

namespace {

// The event values of dcd_nrf5x.c, the same as NRFX_POWER_USB_EVT_* (nrfx_power.h).
constexpr uint32_t USB_EVT_READY = 2;

// The USBD is ready microseconds after ENABLE, before the regulator's READY event even comes
// (nRF52840 PS v1.1 6.35.4), and the crystal starts in about 1 ms (5.4.1); far above both.
constexpr uint32_t WAIT_LIMIT_US = 100000;
constexpr uint32_t WAIT_STEP_US = 100;
// A crystal already asked for by DETECTED (hfclk_enable()) runs within this time.
constexpr uint32_t CLOCK_STARTING_US = 10000;

// Written by the handler (Bluefruit SOC task, or an interrupt before the SoftDevice starts),
// read by the loop.
volatile uint32_t skipped = 0;

// The checks below follow the handler's own helpers (dcd_nrf5x.c is_sd_enabled(),
// hfclk_running(), hfclk_enable()), so they are safe wherever it is called.
bool softDeviceEnabled()
{
    uint8_t enabled = 0;
    (void)sd_softdevice_is_enabled(&enabled);
    return enabled != 0;
}

bool vbusPresent()
{
    if (softDeviceEnabled()) {
        uint32_t status = 0;
        (void)sd_power_usbregstatus_get(&status);
        return (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
    }
    return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

bool usbdReady()
{
    return (NRF_USBD->EVENTCAUSE & USBD_EVENTCAUSE_READY_Msk) != 0;
}

bool clockRunning()
{
    if (softDeviceEnabled()) {
        uint32_t running = 0;
        (void)sd_clock_hfclk_is_running(&running);
        return running != 0;
    }
    return nrf_clock_hf_is_running(NRF_CLOCK, NRF_CLOCK_HFCLK_HIGH_ACCURACY);
}

// Busy wait: the handler may run in an interrupt, where delay() cannot be used.
bool waitFor(bool (*condition)(), uint32_t limitUs)
{
    for (uint32_t waited = 0; waited < limitUs; waited += WAIT_STEP_US) {
        if (condition())
            return true;
        delayMicroseconds(WAIT_STEP_US);
    }
    return condition();
}

void requestClock()
{
    if (softDeviceEnabled()) {
        (void)sd_clock_hfclk_request();
        return;
    }
    nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);
}

void releaseClock()
{
    if (softDeviceEnabled()) {
        (void)sd_clock_hfclk_release();
        return;
    }
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTOP);
}

// The original waits for the crystal but does not ask for it (TinyUSB PR #3633 added that); it
// is asked for here only when DETECTED did not do it, because the crystal was running then (for
// the radio) and has stopped since. *requested tells that this happened. TinyUSB's REMOVED
// releases it (hfclk_disable()) while the USBD is enabled, as it is here.
bool clockStarts(bool *requested)
{
    *requested = false;
    if (waitFor(clockRunning, CLOCK_STARTING_US))
        return true;
    requestClock();
    *requested = true;
    return waitFor(clockRunning, WAIT_LIMIT_US);
}

// Pull-up on and clock running: the original returns at once (Bluefruit.begin() may send READY
// again).
bool alreadyUp()
{
    return NRF_USBD->USBPULLUP && clockRunning();
}

// True when the original handler's waits for READY will end. A crystal asked for here is
// released again when READY is skipped, so nothing keeps it running on the battery.
bool readyCanFinish()
{
    if (!NRF_USBD->ENABLE || !vbusPresent()) // REMOVED handled already, or the cable is gone
        return false;
    if (!waitFor(usbdReady, WAIT_LIMIT_US))
        return false;
    bool requested = false;
    if (clockStarts(&requested))
        return true;
    if (requested)
        releaseClock();
    return false;
}

} // namespace

extern "C" void __wrap_tusb_hal_nrf_power_event(uint32_t event)
{
    if (event == USB_EVT_READY && !alreadyUp() && !readyCanFinish()) {
        skipped = skipped + 1;
        return;
    }
    __real_tusb_hal_nrf_power_event(event);
}

namespace usb_power {

void logState()
{
    logger::transient("usb: power events guarded, %lu READY skipped", (unsigned long)skipped);
}

} // namespace usb_power
