#include "ACAN_STM32.h"
#include "Arduino.h"
#include "candefs.hpp"
#include "utils.hpp"
#include "STM32TimerInterrupt.h"
#include "params.hpp"
#include <atomic>

static void throttle_rcv(const CANMessage &inMessage) {
    // Blink the LED when we receive a CAN message
    digitalToggle(LED_BUILTIN);

    uint8_t percent = inMessage.data[0];

    // Cap max speed
    percent = uint8_t(percent * MAX_SPEED_PERCENT);

    Serial.printf("Received message, setting throttle to %hu %\n", percent);

    // Send motor speed command to controller
    Serial2.printf("!G 1 %hu _", percent * 10);
}

static void steering_rcv(const CANMessage &inMessage) {
    // Blink the LED when we receive a CAN message
    digitalToggle(LED_BUILTIN);

    // Left positive angle in degrees
    float angle = inMessage.dataFloat[0];

    Serial.printf("Received message, setting steering to %f %\n", angle);

    /*
     * We assume the steering motor firmware has configured our max angle as full/0 duty cycle, so we map the range of
     * our steering angles from positive to negative linearly, inverting because left is positive.
     */
    float angle_as_bits = mapfloat(-angle, -MAX_STEERING, MAX_STEERING, 0, 4092.0);

    analogWrite(STEERING_PIN, uint16_t(angle_as_bits));
}

volatile std::atomic<bool> msg_to_send = false;
volatile CANMessage encoder_msg;
volatile std::atomic<uint32_t> ticks = 0;
volatile uint32_t last_ticks = 0;

STM32Timer timer{TIM6};

static void timer_irq() {
    timer.detachInterrupt();
    uint32_t current_count = ticks.load();

    float ticks_per_ms = 0;
    uint32_t ticks_since_last_message = 0;

    // Find tick rate, handling overflow
    if (last_ticks > current_count) {
        uint32_t ticks_pre_overflow = std::numeric_limits<uint32_t>::max() - last_ticks;

        // Total ticks are ticks before we overflowed, and the ticks after
        ticks_since_last_message = ticks_pre_overflow + current_count;
        ticks_per_ms = float(ticks_since_last_message) / ENCODER_SAMPLE_PERIOD_US * 1000;
    } else {
        ticks_since_last_message = current_count - last_ticks;
        ticks_per_ms = float(ticks_since_last_message) / ENCODER_SAMPLE_PERIOD_US * 1000;
    }

    // ticks/sec * ms/s * rot/ticks = rot/sec
    float rps =
            ticks_per_ms * 1000.0f * (1.0f / ENCODER_TEETH);
    float rad_per_sec = rps * 2.0f * PI;

    float meter_per_sec = rad_per_sec * WHEEL_CIRC_METER;

    Serial.printf("Speed: %f m/s\n", meter_per_sec);

    // Setup can message for main loop
    encoder_msg.id = CanID::Encoder;
    encoder_msg.ext = true;
    encoder_msg.len = 6;
    encoder_msg.data16[0] = uint16_t(ticks_since_last_message);
    ((float *) (encoder_msg.data + 2))[0] = meter_per_sec;
    msg_to_send.store(true);

    last_ticks = current_count;
    timer.attachInterruptInterval(ENCODER_SAMPLE_PERIOD_US, timer_irq);
}

static void inc_count() {
    ticks.fetch_add(1);
}

void setup() {
    // Pin setup
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(STEERING_PIN, OUTPUT);
    pinMode(ENCODER_PIN, INPUT_PULLUP);
    pinMode(ROBOTEQ_UART_RX, OUTPUT);
    pinMode(ROBOTEQ_UART_TX, OUTPUT);

    Serial.begin(115200);

    // Roboteq serial
    Serial2.begin(115200, SERIAL_8N1);

    // Check for presence of roboteq with query
    Serial2.write((char) 0x5);
    if (Serial2.read() == 0x6) {
        Serial.println("Roboteq detected!");
    } else {
        Serial.println("Roboteq not detected :(");
    }

    // Our bus is 512k baud
    ACAN_STM32_Settings sett{500'000};
    ACAN_STM32::Filters filters;

    // Throttle control messages
    filters.addExtendedMask(CanID::SetSpeed, CanID::SetSpeed, ACAN_STM32::DATA, throttle_rcv, ACAN_STM32::FIFO0);
    // Steering control messages
    filters.addExtendedMask(CanID::SetAngle, CanID::SetAngle, ACAN_STM32::DATA, steering_rcv, ACAN_STM32::FIFO0);

    if (can.begin(sett, filters)) {
        Serial.println("Can OK");
    }

    // Set our outputs to 12-bit resolution 0-4096
    analogWriteResolution(DAC_BITS);

    // Setup timer for encoders (5ms)
    if (timer.attachInterruptInterval(ENCODER_SAMPLE_PERIOD_US, timer_irq)) {
        Serial.printf("Timer started successfully with period: %u", ENCODER_SAMPLE_PERIOD_US);
    } else {
        Serial.println("Timer init failed!");
    }

    // Encoder interrupt
    attachInterrupt(ENCODER_PIN, inc_count, RISING);
}

void loop() {
    can.dispatchReceivedMessage();

    // Critical section over can message
    noInterrupts();
    if (msg_to_send.load()) {
        can.tryToSendReturnStatus(const_cast<const CANMessage &>(encoder_msg));
        msg_to_send.store(false);
    }
    interrupts();
}