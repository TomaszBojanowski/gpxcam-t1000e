// Host tests of the state machines against fakes (fake.h): pio test -e native_sessions
// Each test starts from fake::reset(); a failing test prints the card's log.
#include <unity.h>

#include "fake.h"

void runCameraSessionTests();
void runLinkTests();
void runPairingTests();
void runOlderSessionTests();
void runInvariantTests();
void runSessionEndTests();

void setUp()
{
    fake::reset();
}

void tearDown()
{
    if (Unity.CurrentTestFailed)
        fake::printLog();
    TEST_ASSERT_EQUAL_MESSAGE(0, fake::longLines(), "a log line longer than the card's 125 characters");
}

int main(int, char **)
{
    UNITY_BEGIN();
    runCameraSessionTests();
    runLinkTests();
    runPairingTests();
    runOlderSessionTests();
    runInvariantTests();
    runSessionEndTests();
    return UNITY_END();
}
