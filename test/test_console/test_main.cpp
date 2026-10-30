// Unit tests for src/console_commands.cpp, run on the PC: pio test -e native
// The console knows every command the card page and the app send (test/commands.json) and every one
// docs/GUIDE.md documents, runs a line only as the command it spells, and its help has the rows of
// the guide's table. Made-up values.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "console_commands.h"

using console_commands::Id;

void setUp() {}
void tearDown() {}

namespace {

// 'pio test' runs the tests in the project folder.
constexpr const char *COMMANDS_FILE = "test/commands.json";
constexpr const char *GUIDE_FILE = "docs/GUIDE.md";
constexpr const char *GUIDE_CONSOLE = "## Serial console (service)";

// The last command of the released firmware (the test build's commands come after it).
constexpr Id LAST_COMMAND = Id::StorageFormatYes;

std::string readFile(const char *path)
{
    std::string text;
    FILE *file = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path);
    char buffer[4096];
    size_t length;
    while ((length = fread(buffer, 1, sizeof(buffer), file)) > 0)
        text.append(buffer, length);
    fclose(file);
    return text;
}

// The strings of one list in test/commands.json ("name": ["...", ...]), with JSON's escapes.
std::vector<std::string> jsonList(const std::string &json, const char *name)
{
    const std::string key = std::string("\"") + name + "\": [";
    size_t at = json.find(key);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, name);
    at += key.size();
    std::vector<std::string> list;
    for (;;) {
        at = json.find_first_not_of(" \r\n,", at);
        TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, name);
        if (json[at] == ']')
            return list;
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('"', json[at], name);
        std::string item;
        for (at++; json[at] != '"'; at++) {
            TEST_ASSERT_TRUE_MESSAGE(at < json.size(), name);
            if (json[at] != '\\') {
                item += json[at];
                continue;
            }
            at++;
            const char escaped = json[at];
            TEST_ASSERT_TRUE_MESSAGE(strchr("nr\"\\", escaped) != nullptr, "an escape the test does not know");
            item += escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped;
        }
        list.push_back(item);
        at++;
    }
}

std::vector<std::string> split(const std::string &text, const std::string &separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t at; (at = text.find(separator, start)) != std::string::npos; start = at + separator.size())
        parts.push_back(text.substr(start, at - start));
    parts.push_back(text.substr(start));
    return parts;
}

// The rows of the console's table in docs/GUIDE.md, each a list of the commands of its first column
// ("`pair` / `pair off`" → pair, pair off).
std::vector<std::vector<std::string>> guideRows()
{
    const std::string guide = readFile(GUIDE_FILE);
    const size_t section = guide.find(GUIDE_CONSOLE);
    TEST_ASSERT_TRUE_MESSAGE(section != std::string::npos, GUIDE_CONSOLE);
    std::vector<std::vector<std::string>> rows;
    size_t at = guide.find("\n| `", section);
    while (at != std::string::npos && guide.compare(at, 4, "\n| `") == 0) {
        const size_t cellStart = at + 3;
        const size_t cellEnd = guide.find(" |", cellStart);
        std::vector<std::string> commands = split(guide.substr(cellStart, cellEnd - cellStart), " / ");
        for (std::string &command : commands) {
            TEST_ASSERT_TRUE_MESSAGE(command.size() > 2 && command.front() == '`' && command.back() == '`',
                                     command.c_str());
            command = command.substr(1, command.size() - 2);
        }
        rows.push_back(commands);
        at = guide.find('\n', at + 1);
    }
    TEST_ASSERT_TRUE_MESSAGE(!rows.empty(), "the console's table in docs/GUIDE.md");
    return rows;
}

std::vector<std::string> helpLines;

void collectHelpLine(const char *line)
{
    helpLines.push_back(line);
}

std::vector<std::string> printHelp()
{
    helpLines.clear();
    console_commands::help(collectHelpLine);
    return helpLines;
}

std::string lower(std::string text)
{
    for (char &c : text)
        c = (char)tolower((unsigned char)c);
    return text;
}

std::string withoutSpaces(const std::string &text, bool atEnd)
{
    const size_t start = text.find_first_not_of(' ');
    if (start == std::string::npos)
        return "";
    const size_t end = atEnd ? text.find_last_not_of(' ') + 1 : text.size();
    return text.substr(start, end - start);
}

std::string trim(const std::string &text)
{
    return withoutSpaces(text, true);
}

struct Found {
    Id id;
    std::string argument;
};

Found find(const std::string &line)
{
    char buffer[128];
    TEST_ASSERT_LESS_THAN_MESSAGE(sizeof(buffer), line.size(), line.c_str());
    strcpy(buffer, line.c_str());
    const console_commands::Command command = console_commands::find(buffer);
    return {command.id, command.argument};
}

// The console runs a line only as the command it spells: the command's text in any case, and for one
// with an argument, the argument after it. Nothing else may turn into a command.
void assertSpells(const std::string &line, const Found &found)
{
    const std::string typed = trim(line);
    if (found.id == Id::Empty || found.id == Id::Unknown) {
        TEST_ASSERT_EQUAL_MESSAGE(typed.empty(), found.id == Id::Empty, line.c_str());
        return;
    }
    const std::string text = console_commands::text(found.id);
    if (text.back() != ' ') {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(text.c_str(), lower(typed).c_str(), line.c_str());
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", found.argument.c_str(), line.c_str());
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(lower(typed).compare(0, text.size(), text) == 0, line.c_str());
    const std::string rest = typed.substr(text.size());
    TEST_ASSERT_FALSE_MESSAGE(found.argument.empty(), line.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(lower(withoutSpaces(rest, false)).c_str(),
                                     lower(withoutSpaces(found.argument, false)).c_str(), line.c_str());
}

bool isCommand(Id id)
{
    return id != Id::Empty && id != Id::Unknown;
}

// A command of the guide with its placeholder (an upper-case last word) replaced by a made-up value.
std::string withSample(const std::string &command)
{
    const size_t space = command.rfind(' ');
    if (space == std::string::npos)
        return command;
    const std::string last = command.substr(space + 1);
    if (lower(last) == last)
        return command;
    const char *sample = last == "NAME" ? "America/New_York"
                         : last == "RULE" ? "<+0545>-5:45"
                         : last == "N"    ? "10"
                         : last == "S"    ? "5"
                                          : nullptr;
    TEST_ASSERT_NOT_NULL_MESSAGE(sample, ("a placeholder the test does not know: " + command).c_str());
    return command.substr(0, space + 1) + sample;
}

} // namespace

void test_every_command_finds_itself()
{
    for (uint8_t i = (uint8_t)Id::Version; i <= (uint8_t)LAST_COMMAND; i++) {
        const Id id = (Id)i;
        const char *text = console_commands::text(id);
        TEST_ASSERT_NOT_NULL(text);
        const bool takesArgument = text[strlen(text) - 1] == ' ';
        const Found found = find(takesArgument ? std::string(text) + "x" : text);
        TEST_ASSERT_EQUAL_MESSAGE(id, found.id, text);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(takesArgument ? "x" : "", found.argument.c_str(), text);
    }
    TEST_ASSERT_NULL(console_commands::text(Id::Empty));
    TEST_ASSERT_NULL(console_commands::text(Id::Unknown));
}

// Everything the card page and the app send is a command of the console, the one it spells.
void test_the_commands_of_the_page_and_the_app_are_known()
{
    const std::string json = readFile(COMMANDS_FILE);
    for (const std::string &command : jsonList(json, "fixed")) {
        const Found found = find(command);
        TEST_ASSERT_TRUE_MESSAGE(isCommand(found.id), command.c_str());
        assertSpells(command, found);
    }
    for (const std::string &command : jsonList(json, "allowed")) {
        const Found found = find(command);
        TEST_ASSERT_EQUAL_MESSAGE(Id::TimeZoneSet, found.id, command.c_str());
        TEST_ASSERT_EQUAL_STRING(command.substr(strlen(console_commands::TIME_ZONE_SET)).c_str(),
                                 found.argument.c_str());
    }
}

// What the page and the app never send does not turn into another command. CR and LF end a line of the
// console (main.cpp handleChar()), so a text with them is several lines.
void test_refused_commands_stay_what_they_spell()
{
    const std::string json = readFile(COMMANDS_FILE);
    for (const std::string &refused : jsonList(json, "refused"))
        for (const std::string &crLine : split(refused, "\r"))
            for (const std::string &line : split(crLine, "\n"))
                assertSpells(line, find(line));
    TEST_ASSERT_EQUAL(Id::Unknown, find("pair on").id);
    TEST_ASSERT_EQUAL(Id::Unknown, find("unpair now").id);
    TEST_ASSERT_EQUAL(Id::Unknown, find("diag").id);
    TEST_ASSERT_EQUAL(Id::Unknown, find("diag  scan").id);
    TEST_ASSERT_EQUAL(Id::Unknown, find("clock  on").id);
    // The setting commands take any word; their settings refuse an unknown one (sound_rules, link_policy).
    const Found sound = find("sound on");
    TEST_ASSERT_EQUAL(Id::SoundSet, sound.id);
    TEST_ASSERT_EQUAL_STRING("on", sound.argument.c_str());
    const Found updates = find("updates  normal");
    TEST_ASSERT_EQUAL(Id::UpdatesSet, updates.id);
    TEST_ASSERT_EQUAL_STRING(" normal", updates.argument.c_str());
}

void test_the_guide_commands_are_known()
{
    for (const std::vector<std::string> &row : guideRows())
        for (const std::string &command : row) {
            const std::string line = withSample(command);
            const Found found = find(line);
            TEST_ASSERT_TRUE_MESSAGE(isCommand(found.id), line.c_str());
            assertSpells(line, found);
        }
}

// The help has the rows of the guide's table, in its order: "  pair / pair off = ...", and lines that go
// on with a deeper indent.
void test_the_help_has_the_rows_of_the_guide_table()
{
    std::vector<std::vector<std::string>> helpRows;
    for (const std::string &line : printHelp()) {
        if (line.compare(0, 2, "  ") != 0 || line[2] == ' ')
            continue;
        const size_t equals = line.find(" = ");
        TEST_ASSERT_TRUE_MESSAGE(equals != std::string::npos, line.c_str());
        helpRows.push_back(split(line.substr(2, equals - 2), " / "));
    }
    const std::vector<std::vector<std::string>> guide = guideRows();
    TEST_ASSERT_EQUAL(guide.size(), helpRows.size());
    for (size_t i = 0; i < guide.size(); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(guide[i].size(), helpRows[i].size(), guide[i][0].c_str());
        for (size_t j = 0; j < guide[i].size(); j++)
            TEST_ASSERT_EQUAL_STRING(guide[i][j].c_str(), helpRows[i][j].c_str());
    }
}

void test_help_lines_fit_a_log_line()
{
    const std::vector<std::string> lines = printHelp();
    TEST_ASSERT_EQUAL_STRING("commands:", lines.at(0).c_str());
    for (const std::string &line : lines)
        TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(console_commands::HELP_LINE_MAX, line.size(), line.c_str());
}

void test_case_and_spaces()
{
    TEST_ASSERT_EQUAL(Id::Version, find("  V  ").id);
    TEST_ASSERT_EQUAL(Id::PairOlder, find("Pair Older").id);
    TEST_ASSERT_EQUAL(Id::Empty, find("").id);
    TEST_ASSERT_EQUAL(Id::Empty, find("   ").id);
    TEST_ASSERT_EQUAL(Id::TimeZone, find("TZ").id);
    const Found sound = find("SOUND QUIET ");
    TEST_ASSERT_EQUAL(Id::SoundSet, sound.id);
    TEST_ASSERT_EQUAL_STRING("quiet", sound.argument.c_str());
    const Found clock = find("diag clk 5");
    TEST_ASSERT_EQUAL(Id::DiagClockLater, clock.id);
    TEST_ASSERT_EQUAL_STRING("5", clock.argument.c_str());
}

// POSIX rules need their case ("M3.5.0"); 'tz' skips the spaces before its argument.
void test_the_time_zone_keeps_its_case()
{
    const Found rule = find("Tz  <+0545>-5:45 ");
    TEST_ASSERT_EQUAL(Id::TimeZoneSet, rule.id);
    TEST_ASSERT_EQUAL_STRING("<+0545>-5:45", rule.argument.c_str());
    const Found summerTime = find("tz CET-1CEST,M3.5.0,M10.5.0/3");
    TEST_ASSERT_EQUAL(Id::TimeZoneSet, summerTime.id);
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", summerTime.argument.c_str());
    TEST_ASSERT_EQUAL(Id::Unknown, find("tzx").id);
}

// The console prints the line after "command: unknown", in lower case.
void test_an_unknown_line_comes_back_in_lower_case()
{
    char line[] = "  Hello World ";
    const console_commands::Command command = console_commands::find(line);
    TEST_ASSERT_EQUAL(Id::Unknown, command.id);
    TEST_ASSERT_EQUAL_STRING("hello world", command.argument);
    TEST_ASSERT_EQUAL(Id::Unknown, find("v2").id);
    TEST_ASSERT_EQUAL(Id::Unknown, find("diag snd").id);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_command_finds_itself);
    RUN_TEST(test_the_commands_of_the_page_and_the_app_are_known);
    RUN_TEST(test_refused_commands_stay_what_they_spell);
    RUN_TEST(test_the_guide_commands_are_known);
    RUN_TEST(test_the_help_has_the_rows_of_the_guide_table);
    RUN_TEST(test_help_lines_fit_a_log_line);
    RUN_TEST(test_case_and_spaces);
    RUN_TEST(test_the_time_zone_keeps_its_case);
    RUN_TEST(test_an_unknown_line_comes_back_in_lower_case);
    return UNITY_END();
}
