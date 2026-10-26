/**
 * @file TestFramework.h
 * @brief A deliberately tiny test framework: TEST_CASE registers a test, EXPECT checks a condition.
 *
 * Kept dependency-free so the engine tests build anywhere in seconds.
 * Run all tests with `midi_forge_engine_tests`, or one group with `midi_forge_engine_tests <name filter>`.
 */
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testing
{

struct RegisteredTest
{
    std::string name;
    std::function<void()> body;
};

/** All tests, in registration order. */
inline std::vector<RegisteredTest>& allTests()
{
    static std::vector<RegisteredTest> tests;
    return tests;
}

/** Number of failed EXPECTs so far. */
inline int& failureCount()
{
    static int count = 0;
    return count;
}

/** Registers a test when constructed at namespace scope. */
struct TestRegistration
{
    TestRegistration (const char* name, std::function<void()> body)
    {
        allTests().push_back ({ name, std::move (body) });
    }
};

/** Runs all tests whose name contains the filter text. @return The process exit code. */
inline int runAllTests (const std::string& nameFilter)
{
    int numRun = 0;
    for (const auto& test : allTests())
    {
        if (! nameFilter.empty() && test.name.find (nameFilter) == std::string::npos)
            continue;
        const int failuresBefore = failureCount();
        test.body();
        ++numRun;
        std::printf ("%s  %s\n", failureCount() == failuresBefore ? "[pass]" : "[FAIL]", test.name.c_str());
    }
    std::printf ("\n%d test(s) run, %d failed check(s).\n", numRun, failureCount());
    return failureCount() == 0 && numRun > 0 ? 0 : 1;
}

} // namespace testing

#define TESTING_CONCATENATE_INNER(first, second) first##second
#define TESTING_CONCATENATE(first, second) TESTING_CONCATENATE_INNER (first, second)

/** Defines and registers a test: TEST_CASE ("description") { ...checks... } */
#define TEST_CASE(testName)                                                                                            \
    static void TESTING_CONCATENATE (testFunction, __LINE__)();                                                        \
    static const testing::TestRegistration TESTING_CONCATENATE (testRegistration, __LINE__) (                          \
        testName, TESTING_CONCATENATE (testFunction, __LINE__));                                                       \
    static void TESTING_CONCATENATE (testFunction, __LINE__)()

/** Checks a condition; on failure prints it with file and line and counts the failure. */
#define EXPECT(condition)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (! (condition))                                                                                             \
        {                                                                                                              \
            std::printf ("    check failed at %s:%d:  %s\n", __FILE__, __LINE__, #condition);                          \
            ++testing::failureCount();                                                                                 \
        }                                                                                                              \
    } while (false)
