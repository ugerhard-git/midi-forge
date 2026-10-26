/**
 * @file EngineTestMain.cpp
 * @brief Entry point of the engine tests. Usage: midi_forge_engine_tests [name filter]
 */
#include "../TestFramework.h"

int main (int argc, char** argv)
{
    return testing::runAllTests (argc > 1 ? argv[1] : "");
}
