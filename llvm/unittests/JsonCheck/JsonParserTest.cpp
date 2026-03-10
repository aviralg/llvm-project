//===--- JsonParserTest.cpp - Unit tests for JsonCheck parser -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/JsonCheck/JsonParser.h"
#include "gtest/gtest.h"
#include <string>

using namespace llvm;
using namespace llvm::jsoncheck;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Parse and assert success; return the root Value.
static Value mustParse(StringRef Input) {
  auto E = parse(Input);
  EXPECT_TRUE(bool(E)) << toString(E.takeError());
  if (!E) {
    // Return a sentinel null to avoid crashing after failed EXPECT.
    return Value::makeNull({});
  }
  return std::move(*E);
}

/// Parse and assert failure; return the error message string.
static std::string mustFail(StringRef Input) {
  auto E = parse(Input);
  EXPECT_FALSE(bool(E));
  if (!E)
    return toString(E.takeError());
  return "";
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

TEST(JsonParser, Null) {
  auto V = mustParse("null");
  EXPECT_EQ(V.getKind(), Kind::Null);
  EXPECT_TRUE(V.isNull());
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   4u);
}

TEST(JsonParser, BooleanTrue) {
  auto V = mustParse("true");
  EXPECT_EQ(V.getKind(), Kind::Boolean);
  ASSERT_TRUE(V.getAsBoolean().has_value());
  EXPECT_EQ(*V.getAsBoolean(), true);
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   4u);
}

TEST(JsonParser, BooleanFalse) {
  auto V = mustParse("false");
  EXPECT_EQ(V.getKind(), Kind::Boolean);
  ASSERT_TRUE(V.getAsBoolean().has_value());
  EXPECT_EQ(*V.getAsBoolean(), false);
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   5u);
}

TEST(JsonParser, Integer) {
  auto V = mustParse("42");
  EXPECT_EQ(V.getKind(), Kind::Integer);
  ASSERT_TRUE(V.getAsInteger().has_value());
  EXPECT_EQ(*V.getAsInteger(), 42LL);
}

TEST(JsonParser, NegativeInteger) {
  auto V = mustParse("-7");
  EXPECT_EQ(V.getKind(), Kind::Integer);
  ASSERT_TRUE(V.getAsInteger().has_value());
  EXPECT_EQ(*V.getAsInteger(), -7LL);
}

TEST(JsonParser, Float) {
  auto V = mustParse("3.14");
  EXPECT_EQ(V.getKind(), Kind::Float);
  ASSERT_TRUE(V.getAsFloat().has_value());
  EXPECT_DOUBLE_EQ(*V.getAsFloat(), 3.14);
}

TEST(JsonParser, FloatExponent) {
  auto V = mustParse("1e10");
  EXPECT_EQ(V.getKind(), Kind::Float);
  ASSERT_TRUE(V.getAsFloat().has_value());
  EXPECT_DOUBLE_EQ(*V.getAsFloat(), 1e10);
}

TEST(JsonParser, String) {
  auto V = mustParse(R"("hello")");
  EXPECT_EQ(V.getKind(), Kind::String);
  ASSERT_TRUE(V.getAsString().has_value());
  EXPECT_EQ(*V.getAsString(), "hello");
}

TEST(JsonParser, StringEscapes) {
  auto V = mustParse(R"("a\nb\tc\"d\\e")");
  EXPECT_EQ(V.getKind(), Kind::String);
  ASSERT_TRUE(V.getAsString().has_value());
  EXPECT_EQ(*V.getAsString(), "a\nb\tc\"d\\e");
}

TEST(JsonParser, StringUnicodeEscape) {
  auto V = mustParse(R"("\u0041")"); // U+0041 = 'A'
  EXPECT_EQ(V.getKind(), Kind::String);
  ASSERT_TRUE(V.getAsString().has_value());
  EXPECT_EQ(*V.getAsString(), "A");
}

// ---------------------------------------------------------------------------
// Standard structures
// ---------------------------------------------------------------------------

TEST(JsonParser, EmptyArray) {
  auto V = mustParse("[]");
  EXPECT_EQ(V.getKind(), Kind::Array);
  ASSERT_NE(V.getAsArray(), nullptr);
  EXPECT_TRUE(V.getAsArray()->empty());
}

TEST(JsonParser, SimpleArray) {
  auto V = mustParse("[1, 2, 3]");
  EXPECT_EQ(V.getKind(), Kind::Array);
  const Array *A = V.getAsArray();
  ASSERT_NE(A, nullptr);
  ASSERT_EQ(A->size(), 3u);
  EXPECT_EQ(*(*A)[0].getAsInteger(), 1LL);
  EXPECT_EQ(*(*A)[1].getAsInteger(), 2LL);
  EXPECT_EQ(*(*A)[2].getAsInteger(), 3LL);
}

TEST(JsonParser, NestedArray) {
  auto V = mustParse("[[1, 2], [3]]");
  EXPECT_EQ(V.getKind(), Kind::Array);
  const Array *A = V.getAsArray();
  ASSERT_NE(A, nullptr);
  ASSERT_EQ(A->size(), 2u);
  EXPECT_EQ((*A)[0].getKind(), Kind::Array);
  EXPECT_EQ((*A)[1].getKind(), Kind::Array);
}

TEST(JsonParser, EmptyObject) {
  auto V = mustParse("{}");
  EXPECT_EQ(V.getKind(), Kind::Object);
  ASSERT_NE(V.getAsObject(), nullptr);
  EXPECT_TRUE(V.getAsObject()->empty());
}

TEST(JsonParser, SimpleObject) {
  auto V = mustParse(R"({"a": 1, "b": true})");
  EXPECT_EQ(V.getKind(), Kind::Object);
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 2u);
  EXPECT_EQ((*O)[0].Key.Value, "a");
  EXPECT_EQ((*O)[1].Key.Value, "b");
}

TEST(JsonParser, NestedObject) {
  auto V = mustParse(R"({"x": {"y": 42}})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 1u);
  ASSERT_NE((*O)[0].Val, nullptr);
  EXPECT_EQ((*O)[0].Val->getKind(), Kind::Object);
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------

TEST(JsonParser, LineComment) {
  auto V = mustParse("// comment\n42");
  EXPECT_EQ(V.getKind(), Kind::Integer);
  EXPECT_EQ(*V.getAsInteger(), 42LL);
}

TEST(JsonParser, BlockComment) {
  auto V = mustParse("/* comment */ 42");
  EXPECT_EQ(V.getKind(), Kind::Integer);
  EXPECT_EQ(*V.getAsInteger(), 42LL);
}

TEST(JsonParser, CommentInObject) {
  auto V = mustParse(R"({"a": /* inner */ 1})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 1u);
  EXPECT_EQ(*(*O)[0].Val->getAsInteger(), 1LL);
}

TEST(JsonParser, CommentAtEOF) {
  auto V = mustParse("42 // eof");
  EXPECT_EQ(V.getKind(), Kind::Integer);
}

TEST(JsonParser, LineCommentNoNewline) {
  // Line comment at the very end without trailing newline.
  auto V = mustParse("null // no newline");
  EXPECT_EQ(V.getKind(), Kind::Null);
}

TEST(JsonParser, MultipleBlockComments) {
  auto V = mustParse("/* a *//* b */null");
  EXPECT_EQ(V.getKind(), Kind::Null);
}

// ---------------------------------------------------------------------------
// Trailing commas
// ---------------------------------------------------------------------------

TEST(JsonParser, TrailingCommaArray) {
  auto V = mustParse("[1, 2,]");
  const Array *A = V.getAsArray();
  ASSERT_NE(A, nullptr);
  EXPECT_EQ(A->size(), 2u);
}

TEST(JsonParser, TrailingCommaObject) {
  auto V = mustParse(R"({"a": 1,})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  EXPECT_EQ(O->size(), 1u);
}

// ---------------------------------------------------------------------------
// Source ranges
// ---------------------------------------------------------------------------

TEST(JsonParser, RangeNull) {
  auto V = mustParse("  null");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   6u);
}

TEST(JsonParser, RangeBoolean) {
  auto V = mustParse("  true");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   6u);
}

TEST(JsonParser, RangeInteger) {
  auto V = mustParse("  123");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   5u);
}

TEST(JsonParser, RangeString) {
  // "hi" = offset 0..4 (including quotes).
  auto V = mustParse(R"("hi")");
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   4u);
}

TEST(JsonParser, RangeArray) {
  auto V = mustParse("[1]");
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   3u);
}

TEST(JsonParser, RangeObject) {
  auto V = mustParse(R"({"k":1})");
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   7u);
}

TEST(JsonParser, RangeArrayElements) {
  auto V = mustParse("[1, 22, 333]");
  const Array *A = V.getAsArray();
  ASSERT_NE(A, nullptr);
  EXPECT_EQ((*A)[0].getRange().Begin, 1u);
  EXPECT_EQ((*A)[0].getRange().End,   2u);
  EXPECT_EQ((*A)[1].getRange().Begin, 4u);
  EXPECT_EQ((*A)[1].getRange().End,   6u);
  EXPECT_EQ((*A)[2].getRange().Begin, 8u);
  EXPECT_EQ((*A)[2].getRange().End,  11u);
}

// ---------------------------------------------------------------------------
// Object key ranges
// ---------------------------------------------------------------------------

TEST(JsonParser, KeyRangeRegular) {
  auto V = mustParse(R"({"ab": 1})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  const ObjectKey &Key = (*O)[0].Key;
  EXPECT_EQ(Key.Kind, KeyKind::Regular);
  EXPECT_EQ(Key.Value, "ab");
  // Key range: from '"' to closing '"' inclusive → offsets 1..4 ("ab")
  EXPECT_EQ(Key.Range.Begin, 1u);
  EXPECT_EQ(Key.Range.End,   5u);
}

TEST(JsonParser, KeyRangeAbsent) {
  auto V = mustParse(R"({"a": 1, !"b"})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 2u);
  const Member &AbsMember = (*O)[1];
  EXPECT_EQ(AbsMember.Key.Kind, KeyKind::Absent);
  EXPECT_EQ(AbsMember.Key.Value, "b");
  EXPECT_EQ(AbsMember.Val, nullptr);
  // Range begins at '!' character.
  EXPECT_EQ(AbsMember.Key.Range.Begin, 9u);
}

// ---------------------------------------------------------------------------
// Pattern values
// ---------------------------------------------------------------------------

TEST(JsonParser, Wildcard) {
  auto V = mustParse("&");
  EXPECT_EQ(V.getKind(), Kind::Wildcard);
  EXPECT_TRUE(V.isWildcard());
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   1u);
}

TEST(JsonParser, WildcardInArray) {
  auto V = mustParse("[&, &]");
  const Array *A = V.getAsArray();
  ASSERT_NE(A, nullptr);
  ASSERT_EQ(A->size(), 2u);
  EXPECT_EQ((*A)[0].getKind(), Kind::Wildcard);
  EXPECT_EQ((*A)[1].getKind(), Kind::Wildcard);
}

TEST(JsonParser, Capture) {
  auto V = mustParse("&myVar");
  EXPECT_EQ(V.getKind(), Kind::Capture);
  ASSERT_TRUE(V.getAsString().has_value());
  EXPECT_EQ(*V.getAsString(), "myVar");
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   6u);
}

TEST(JsonParser, CaptureWithUnderscore) {
  auto V = mustParse("&_foo_42");
  EXPECT_EQ(V.getKind(), Kind::Capture);
  EXPECT_EQ(*V.getAsString(), "_foo_42");
}

TEST(JsonParser, Regex) {
  auto V = mustParse(R"(#"[0-9]+")");
  EXPECT_EQ(V.getKind(), Kind::Regex);
  ASSERT_TRUE(V.getAsString().has_value());
  EXPECT_EQ(*V.getAsString(), "[0-9]+");
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   9u);
}

TEST(JsonParser, RegexWithSpaces) {
  auto V = mustParse(R"(#"hello world")");
  EXPECT_EQ(V.getKind(), Kind::Regex);
  EXPECT_EQ(*V.getAsString(), "hello world");
}

TEST(JsonParser, Substitution) {
  auto V = mustParse("<<FOO>>");
  EXPECT_EQ(V.getKind(), Kind::Substitution);
  ASSERT_TRUE(V.getAsString().has_value());
  EXPECT_EQ(*V.getAsString(), "FOO");
  EXPECT_EQ(V.getRange().Begin, 0u);
  EXPECT_EQ(V.getRange().End,   7u);
}

TEST(JsonParser, SubstitutionInArray) {
  auto V = mustParse("[<<A>>, <<B>>]");
  const Array *A = V.getAsArray();
  ASSERT_NE(A, nullptr);
  ASSERT_EQ(A->size(), 2u);
  EXPECT_EQ((*A)[0].getKind(), Kind::Substitution);
  EXPECT_EQ(*(*A)[0].getAsString(), "A");
  EXPECT_EQ((*A)[1].getKind(), Kind::Substitution);
  EXPECT_EQ(*(*A)[1].getAsString(), "B");
}

// ---------------------------------------------------------------------------
// Pattern keys
// ---------------------------------------------------------------------------

TEST(JsonParser, WildcardKey) {
  auto V = mustParse(R"({"a": 1, &: 2})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 2u);
  EXPECT_EQ((*O)[1].Key.Kind, KeyKind::Wildcard);
  ASSERT_NE((*O)[1].Val, nullptr);
  EXPECT_EQ(*(*O)[1].Val->getAsInteger(), 2LL);
}

TEST(JsonParser, CaptureKey) {
  auto V = mustParse(R"({&name: "v"})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 1u);
  EXPECT_EQ((*O)[0].Key.Kind, KeyKind::Capture);
  EXPECT_EQ((*O)[0].Key.Value, "name");
}

TEST(JsonParser, RegexKey) {
  auto V = mustParse(R"({#"k.*": 99})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 1u);
  EXPECT_EQ((*O)[0].Key.Kind, KeyKind::Regex);
  EXPECT_EQ((*O)[0].Key.Value, "k.*");
}

TEST(JsonParser, SubstitutionKey) {
  auto V = mustParse(R"({<<KEY>>: true})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 1u);
  EXPECT_EQ((*O)[0].Key.Kind, KeyKind::Substitution);
  EXPECT_EQ((*O)[0].Key.Value, "KEY");
}

TEST(JsonParser, AbsenceKey) {
  auto V = mustParse(R"({"a": 1, !"b"})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 2u);
  const Member &M = (*O)[1];
  EXPECT_EQ(M.Key.Kind, KeyKind::Absent);
  EXPECT_EQ(M.Key.Value, "b");
  EXPECT_EQ(M.Val, nullptr);
}

// ---------------------------------------------------------------------------
// Mixed patterns
// ---------------------------------------------------------------------------

TEST(JsonParser, AbsentAndWildcard) {
  auto V = mustParse(R"({"present": &, !"missing"})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_EQ(O->size(), 2u);
  EXPECT_EQ((*O)[0].Key.Kind, KeyKind::Regular);
  EXPECT_EQ((*O)[0].Val->getKind(), Kind::Wildcard);
  EXPECT_EQ((*O)[1].Key.Kind, KeyKind::Absent);
  EXPECT_EQ((*O)[1].Val, nullptr);
}

TEST(JsonParser, CaptureInNestedObject) {
  auto V = mustParse(R"({"outer": {"inner": &captured}})");
  const Object *O = V.getAsObject();
  ASSERT_NE(O, nullptr);
  ASSERT_NE((*O)[0].Val, nullptr);
  const Object *Inner = (*O)[0].Val->getAsObject();
  ASSERT_NE(Inner, nullptr);
  ASSERT_EQ(Inner->size(), 1u);
  EXPECT_EQ((*Inner)[0].Val->getKind(), Kind::Capture);
  EXPECT_EQ(*(*Inner)[0].Val->getAsString(), "captured");
}

// ---------------------------------------------------------------------------
// Number parsing edge cases
// ---------------------------------------------------------------------------

TEST(JsonParser, NumberInt64Max) {
  auto V = mustParse("9223372036854775807"); // INT64_MAX
  EXPECT_EQ(V.getKind(), Kind::Integer);
  EXPECT_EQ(*V.getAsInteger(), std::numeric_limits<int64_t>::max());
}

TEST(JsonParser, NumberLargeUInt) {
  // 2^63 overflows int64 but fits in uint64; we represent as float.
  auto V = mustParse("9223372036854775808");
  // Parsed as either Float (if no uint64 path) or Integer depending on impl.
  EXPECT_TRUE(V.getKind() == Kind::Integer || V.getKind() == Kind::Float);
}

TEST(JsonParser, NumberZero) {
  auto V = mustParse("0");
  EXPECT_EQ(V.getKind(), Kind::Integer);
  EXPECT_EQ(*V.getAsInteger(), 0LL);
}

// ---------------------------------------------------------------------------
// Parse errors
// ---------------------------------------------------------------------------

TEST(JsonParser, ErrorEOFInString) {
  EXPECT_FALSE(mustFail(R"("unterminated)").empty());
}

TEST(JsonParser, ErrorBadEscape) {
  EXPECT_FALSE(mustFail(R"("\q")").empty());
}

TEST(JsonParser, ErrorUnterminatedBlockComment) {
  EXPECT_FALSE(mustFail("/* never closed").empty());
}

TEST(JsonParser, ErrorBadSubstitution) {
  EXPECT_FALSE(mustFail("<<noclose").empty());
}

TEST(JsonParser, ErrorDuplicateKey) {
  EXPECT_FALSE(mustFail(R"({"a": 1, "a": 2})").empty());
}

TEST(JsonParser, ErrorTextAfterDocument) {
  EXPECT_FALSE(mustFail("42 garbage").empty());
}

TEST(JsonParser, ErrorMissingColon) {
  EXPECT_FALSE(mustFail(R"({"key" 42})").empty());
}

TEST(JsonParser, ErrorBadAbsence) {
  // '!' without a string key.
  EXPECT_FALSE(mustFail(R"({!42})").empty());
}

TEST(JsonParser, ErrorBadRegex) {
  // '#' without following '"'.
  EXPECT_FALSE(mustFail("#notquoted").empty());
}

TEST(JsonParser, ErrorEmptySubstitution) {
  EXPECT_FALSE(mustFail("<<>>").empty());
}

// ---------------------------------------------------------------------------
// Error position (offsetToLineCol)
// ---------------------------------------------------------------------------

TEST(JsonParser, OffsetToLineColFirstLine) {
  auto [L, C] = offsetToLineCol("hello", 3);
  EXPECT_EQ(L, 1u);
  EXPECT_EQ(C, 4u);
}

TEST(JsonParser, OffsetToLineColSecondLine) {
  auto [L, C] = offsetToLineCol("ab\ncd", 4);
  EXPECT_EQ(L, 2u);
  EXPECT_EQ(C, 2u);
}

TEST(JsonParser, OffsetToLineColAtNewline) {
  // Offset points exactly to the newline character.
  auto [L, C] = offsetToLineCol("ab\ncd", 2);
  EXPECT_EQ(L, 1u);
  EXPECT_EQ(C, 3u);
}

TEST(JsonParser, ErrorReportsLineColumn) {
  // Position check: the error on the bad token should mention line/column.
  auto E = parse("{\n  \"key\": BADINPUT\n}");
  EXPECT_FALSE(bool(E));
  if (!E) {
    std::string Msg = toString(E.takeError());
    // Should mention line 2 where BADINPUT is.
    EXPECT_NE(Msg.find("line 2"), std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// Ranges for pattern values
// ---------------------------------------------------------------------------

TEST(JsonParser, RangeWildcard) {
  auto V = mustParse("  &");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   3u);
}

TEST(JsonParser, RangeCapture) {
  auto V = mustParse("  &xyz");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   6u);
}

TEST(JsonParser, RangeRegex) {
  auto V = mustParse(R"(  #"ab")");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   7u);
}

TEST(JsonParser, RangeSubstitution) {
  auto V = mustParse("  <<X>>");
  EXPECT_EQ(V.getRange().Begin, 2u);
  EXPECT_EQ(V.getRange().End,   7u);
}
