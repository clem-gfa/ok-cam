// Test hors materiel de RecordingName.h : nettoyage du prefixe, nom de session, reglages partages.
// Build : g++ -std=c++17 -I../src recording_name_test.cpp -o recording_name_test
#include <cassert>
#include <iostream>

#include "RecordingName.h"

using namespace okcli;

int main() {
    assert(sanitizeFilePrefix("test1") == "test1");
    assert(sanitizeFilePrefix("manip 3 - essai") == "manip_3_-_essai");           // spaces -> '_'
    assert(sanitizeFilePrefix("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij");           // forbidden characters dropped
    assert(sanitizeFilePrefix("../../etc/passwd") == "etcpasswd");                 // no path traversal
    assert(sanitizeFilePrefix("  ..hidden.. ") == "hidden");                       // leading dots / trailing dots, spaces trimmed
    assert(sanitizeFilePrefix("") == "okcam" && sanitizeFilePrefix("???") == "okcam" && sanitizeFilePrefix("x", "y") == "x");
    assert(sanitizeFilePrefix("\x01\x02" "ab\n") == "ab");                            // control characters dropped
    assert(sanitizeFilePrefix("expérience_été") == "expérience_été");              // UTF-8 kept
    const std::string longName(100, 'a');
    assert(sanitizeFilePrefix(longName).size() == 64);
    std::string utf;                                                               // 40 x "é" (2 bytes each) = 80 bytes: cut on a character boundary
    for (int i = 0; i < 40; i++) utf += "\xC3\xA9";
    const std::string cut = sanitizeFilePrefix(utf);
    assert(cut.size() == 64 && (static_cast<unsigned char>(cut[0]) == 0xC3) && cut.size() % 2 == 0);

    std::tm tm{};
    tm.tm_year = 2026 - 1900; tm.tm_mon = 10; tm.tm_mday = 17; tm.tm_hour = 18; tm.tm_min = 3; tm.tm_sec = 9; tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    assert(recordingSession("manip 3", false, t) == "manip_3_20261117_180309");
    assert(recordingSession("manip 3", true, t) == "manip_3_audio_20261117_180309");
    assert(recordingSession("", false, t) == "okcam_20261117_180309");

    RecordingSettings s;
    int calls = 0;
    s.listeners.push_back([&] { calls++; });
    s.setPrefix("a"); s.setPrefix("a"); s.setDir("/tmp"); s.setDir("/tmp");
    assert(calls == 2 && s.prefix == "a" && s.dir == "/tmp");

    std::cout << "TOUS LES TESTS OK\n";
    return 0;
}
