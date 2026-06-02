// Unit tests for the UTF-8 <-> UTF-16 conversion helpers in common_log.hpp.
//
// These guard against the byte-wise truncation/widening bugs that previously
// corrupted non-ASCII plugin names, paths, and messages:
//   - WideToUTF8 used to do `result += (char)*wide++` (truncation)
//   - call sites used `std::wstring(s.begin(), s.end())` (byte widening)
// Both are replaced by real WideCharToMultiByte / MultiByteToWideChar calls.
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include "common_log.hpp"

using PythonFar::WideToUTF8;
using PythonFar::UTF8ToWide;

// ---- WideToUTF8 -------------------------------------------------------------

TEST(WideToUTF8Test, NullReturnsPlaceholder) {
    EXPECT_EQ(WideToUTF8(nullptr), std::string("(null)"));
}

TEST(WideToUTF8Test, EmptyReturnsEmpty) {
    EXPECT_EQ(WideToUTF8(L""), std::string());
}

TEST(WideToUTF8Test, AsciiUnchanged) {
    EXPECT_EQ(WideToUTF8(L"plugin.far.py"), std::string("plugin.far.py"));
}

TEST(WideToUTF8Test, CyrillicProducesMultiByteUtf8) {
    // L"Привет" -> each Cyrillic char is 2 UTF-8 bytes.
    const wchar_t input[] = { 0x041F, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442, 0 };
    std::string utf8 = WideToUTF8(input);
    const unsigned char expected[] = {
        0xD0, 0x9F, 0xD1, 0x80, 0xD0, 0xB8, 0xD0, 0xB2, 0xD0, 0xB5, 0xD1, 0x82
    };
    ASSERT_EQ(utf8.size(), sizeof(expected));
    for (size_t i = 0; i < sizeof(expected); ++i) {
        EXPECT_EQ(static_cast<unsigned char>(utf8[i]), expected[i]) << "byte " << i;
    }
}

TEST(WideToUTF8Test, CjkProducesThreeBytesEach) {
    // L"你好" -> each CJK char is 3 UTF-8 bytes.
    const wchar_t input[] = { 0x4F60, 0x597D, 0 };
    std::string utf8 = WideToUTF8(input);
    EXPECT_EQ(utf8.size(), static_cast<size_t>(6));
}

// ---- UTF8ToWide -------------------------------------------------------------

TEST(UTF8ToWideTest, EmptyReturnsEmpty) {
    EXPECT_TRUE(UTF8ToWide(std::string()).empty());
}

TEST(UTF8ToWideTest, AsciiUnchanged) {
    EXPECT_EQ(UTF8ToWide("plugin.far.py"), std::wstring(L"plugin.far.py"));
}

TEST(UTF8ToWideTest, CyrillicDecodesCorrectly) {
    // UTF-8 bytes for "Привет.far.py".
    std::string utf8 = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82.far.py";
    std::wstring wide = UTF8ToWide(utf8);
    const wchar_t expected[] = {
        0x041F, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442,
        L'.', L'f', L'a', L'r', L'.', L'p', L'y', 0
    };
    EXPECT_EQ(wide, std::wstring(expected));
}

TEST(UTF8ToWideTest, EmbeddedNulIsPreserved) {
    // MultiByteToWideChar is called with an explicit length, so embedded NULs
    // must survive (not terminate the string early).
    std::string utf8("a\0b", 3);
    std::wstring wide = UTF8ToWide(utf8);
    ASSERT_EQ(wide.size(), static_cast<size_t>(3));
    EXPECT_EQ(wide[0], L'a');
    EXPECT_EQ(wide[1], L'\0');
    EXPECT_EQ(wide[2], L'b');
}

// ---- Round trips ------------------------------------------------------------

TEST(Utf8RoundTripTest, WideToUtf8ToWide) {
    const wchar_t original[] = {
        L'P', L'y', 0x041F, 0x0440, 0x4F60, 0x597D, L'.', L'p', L'y', 0
    };
    std::wstring w(original);
    std::string utf8 = WideToUTF8(w.c_str());
    std::wstring back = UTF8ToWide(utf8);
    EXPECT_EQ(back, w);
}

TEST(Utf8RoundTripTest, Utf8ToWideToUtf8) {
    std::string utf8 = "mixed-\xD0\x9F\xD1\x80-\xE4\xBD\xA0-text";
    std::wstring w = UTF8ToWide(utf8);
    std::string back = WideToUTF8(w.c_str());
    EXPECT_EQ(back, utf8);
}

// The old byte-widening bug would have produced a different length and content
// for non-ASCII input; assert the correct conversion differs from it.
TEST(Utf8RoundTripTest, CorrectConversionDiffersFromByteWidening) {
    std::string utf8 = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"; // "Привет"
    std::wstring correct = UTF8ToWide(utf8);
    std::wstring buggy(utf8.begin(), utf8.end()); // the old, broken approach
    EXPECT_NE(correct, buggy);
    EXPECT_EQ(correct.size(), static_cast<size_t>(6));   // 6 code points
    EXPECT_EQ(buggy.size(), static_cast<size_t>(12));    // 12 raw bytes widened
}
