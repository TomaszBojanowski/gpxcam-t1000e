// Host tests of the state machines against fakes (fake.h): pio test -e native_sessions
// Each test starts from fake::reset(); a failing test prints the card's log.
#include <unity.h>

#include "fake.h"

void runCameraSessionTests();
void runLinkTests();
void runPairingTests();

void setUp()
{
    fake::reset();
}

void tearDown()
{
    if (Unity.CurrentTestFailed)
        fake::printLog();
}

int main(int, char **)
{
    UNITY_BEGIN();
    runCameraSessionTests();
    runLinkTests();
    runPairingTests();
    return UNITY_END();
}
