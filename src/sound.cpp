#include "sound.h"

#include <Arduino.h>
#include <HardwarePWM.h>

namespace {

using sound_rules::Kind;
using sound_rules::Setting;

struct Note {
    uint16_t hz; // PAUSE for silence
    uint16_t ms;
};

struct Melody {
    const Note *notes;
    size_t count;
};

template <size_t N> constexpr Melody melody(const Note (&notes)[N])
{
    return {notes, N};
}

// Notes of Meshtastic's melodies (src/buzz/buzz.cpp @ 6d41e279f1f5): NOTE_FS3, NOTE_AS3,
// NOTE_CS4. Meshtastic plays 125/125/250 ms; ours are about 20 % shorter. After each note
// Meshtastic waits 30 % of its length (delay(1.3 * duration)); we do the same.
constexpr uint16_t NOTE_FS3 = 185;
constexpr uint16_t NOTE_AS3 = 233;
constexpr uint16_t NOTE_CS4 = 277;
constexpr uint16_t NOTE_FS4 = 370; // M4: one chord note above the start melody
constexpr uint16_t PAUSE = 0;
constexpr uint32_t GAP_PERCENT = 30;

constexpr Note START_NOTES[] = {{NOTE_FS3, 100}, {NOTE_AS3, 100}, {NOTE_CS4, 150}};
constexpr Note SHUTDOWN_NOTES[] = {{NOTE_CS4, 100}, {NOTE_AS3, 100}, {NOTE_FS3, 150}};
constexpr Note CLICK_NOTES[] = {{NOTE_AS3, 20}}; // Meshtastic's key-press "chirp"
constexpr Note REFUSE_NOTES[] = {{NOTE_AS3, 20}, {PAUSE, 100}, {NOTE_AS3, 20}};
constexpr Note PAIRING_NOTES[] = {{NOTE_CS4, 60}, {PAUSE, 60}, {NOTE_CS4, 60}};
constexpr Note PAIRED_NOTES[] = {{NOTE_AS3, 100}, {NOTE_CS4, 100}, {NOTE_FS4, 200}};
constexpr Note CONNECTED_NOTES[] = {{NOTE_CS4, 60}, {NOTE_FS4, 60}};    // M5
constexpr Note DISCONNECTED_NOTES[] = {{NOTE_FS4, 60}, {NOTE_CS4, 60}}; // M5
constexpr Note TEST_PAUSE_NOTES[] = {{PAUSE, 700}};

// Volume = PWM duty (sound_rules.h).
constexpr uint8_t MAX_DUTY_PERCENT = 50; // never drive the coil for more than half a period

// PWM3: tone() uses PWM2 (core Tone.cpp) and analogWrite() takes the first free module.
HardwarePWM &pwm = HwPWM3;
constexpr uint32_t PWM_TOKEN = 0x7a7a7542; // "Buzz", marks our ownership of the module
constexpr uint8_t PWM_CLOCK_DIV = PWM_PRESCALER_PRESCALER_DIV_16;
constexpr uint32_t PWM_CLOCK_HZ = 16000000 / 16;
// COUNTERTOP = PWM_CLOCK_HZ / f: 2702..5405 for 370..185 Hz, within its 15-bit range.

constexpr size_t QUEUE_MAX = 13; // the longest sequence is the volume test

bool available = false;
Setting currentSetting = Setting::Loud;
bool isPlaying = false;
bool inGap = false;
Melody queue[QUEUE_MAX];
size_t queueLength = 0;
size_t queuePosition = 0;
size_t noteIndex = 0;
uint8_t dutyPercent = 0;
uint32_t phaseEnd = 0;

const Note &currentNote()
{
    return queue[queuePosition].notes[noteIndex];
}

// Stops the PWM; the pin is then driven by its GPIO setting, which is low.
void silence()
{
    pwm.stop();
}

void startNote(const Note &note)
{
    if (note.hz == PAUSE) {
        silence();
    } else {
        const uint16_t top = PWM_CLOCK_HZ / note.hz;
        const uint16_t onTime = max<uint16_t>(1, top * dutyPercent / 100);
        pwm.setMaxValue(top);
        pwm.writePin(PIN_BUZZER, onTime); // also (re)starts the PWM
    }
    phaseEnd = millis() + note.ms;
    inGap = false;
}

void finish()
{
    silence();
    pwm.removePin(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(BUZZER_EN_PIN, LOW); // buzzer supply only while a sound plays
    isPlaying = false;
}

// duty 0: the setting keeps this sound silent.
void play(const Melody *melodies, size_t count, uint8_t duty)
{
    if (!available || duty == 0)
        return;
    if (isPlaying)
        finish(); // a new sound replaces the current one

    queueLength = min(count, QUEUE_MAX);
    for (size_t i = 0; i < queueLength; i++)
        queue[i] = melodies[i];
    queuePosition = 0;
    noteIndex = 0;
    dutyPercent = min(duty, MAX_DUTY_PERCENT);

    digitalWrite(BUZZER_EN_PIN, HIGH);
    pwm.addPin(PIN_BUZZER);
    isPlaying = true;
    startNote(currentNote());
}

// Moves to the next note; returns false at the end of the queue.
bool advance()
{
    noteIndex++;
    if (noteIndex < queue[queuePosition].count)
        return true;
    noteIndex = 0;
    queuePosition++;
    return queuePosition < queueLength;
}

// Plays one melody as loud as the setting gives for its kind.
void play(const Melody &tune, Kind kind)
{
    play(&tune, 1, sound_rules::dutyPercent(currentSetting, kind));
}

} // namespace

namespace sound {

bool begin()
{
    available = pwm.takeOwnership(PWM_TOKEN);
    if (available)
        pwm.setClockDiv(PWM_CLOCK_DIV);
    return available;
}

void setSetting(Setting setting)
{
    currentSetting = setting;
}

void playStart()
{
    play(melody(START_NOTES), Kind::HoldAnswer);
}

void playShutdown()
{
    play(melody(SHUTDOWN_NOTES), Kind::HoldAnswer);
}

void playClick()
{
    play(melody(CLICK_NOTES), Kind::Event);
}

void playRefuse(Kind kind)
{
    play(melody(REFUSE_NOTES), kind);
}

void playPairing()
{
    play(melody(PAIRING_NOTES), Kind::HoldAnswer);
}

void playPaired()
{
    play(melody(PAIRED_NOTES), Kind::Event);
}

void playConnected()
{
    play(melody(CONNECTED_NOTES), Kind::Event);
}

void playDisconnected()
{
    play(melody(DISCONNECTED_NOTES), Kind::Event);
}

bool playTest(uint8_t duty)
{
    if (duty < 1 || duty > MAX_DUTY_PERCENT)
        return false;
    const Melody sequence[] = {melody(CLICK_NOTES),    melody(TEST_PAUSE_NOTES), melody(START_NOTES),
                               melody(TEST_PAUSE_NOTES), melody(SHUTDOWN_NOTES), melody(TEST_PAUSE_NOTES),
                               melody(PAIRING_NOTES),    melody(TEST_PAUSE_NOTES), melody(PAIRED_NOTES),
                               melody(TEST_PAUSE_NOTES), melody(CONNECTED_NOTES),  melody(TEST_PAUSE_NOTES),
                               melody(DISCONNECTED_NOTES)};
    static_assert(sizeof(sequence) / sizeof(sequence[0]) <= QUEUE_MAX, "the test must fit in the queue");
    play(sequence, sizeof(sequence) / sizeof(sequence[0]), duty);
    return true;
}

void poll()
{
    if (!isPlaying || (int32_t)(millis() - phaseEnd) < 0)
        return;
    if (!inGap) {
        silence();
        inGap = true;
        phaseEnd = millis() + currentNote().ms * GAP_PERCENT / 100;
        return;
    }
    if (advance())
        startNote(currentNote());
    else
        finish();
}

bool playing()
{
    return isPlaying;
}

void stop()
{
    if (isPlaying)
        finish();
}

} // namespace sound
