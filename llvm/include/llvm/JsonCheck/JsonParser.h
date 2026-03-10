//===--- JsonParser.h - JsonCheck pattern parser ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Parser for the JsonCheck extended JSON format.
///
/// The format is a superset of JSON that adds pattern-matching extensions:
///   - Line comments  (// ...)
///   - Block comments (/* ... */)
///   - Trailing commas in arrays/objects
///   - Absence assertion  !"key"            (object key position only)
///   - Wildcard           &                 (value or key position)
///   - Capture/binding    &<ident>          (value or key position)
///   - Regex match        #"<pattern>"      (value or key position)
///   - Substitution       <<name>>          (value or key position)
///
/// Every AST node carries [Begin, End) byte offsets into the source string.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_JSONCHECK_JSONPARSER_H
#define LLVM_JSONCHECK_JSONPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {
namespace jsoncheck {

// ---------------------------------------------------------------------------
// SourceRange
// ---------------------------------------------------------------------------

/// Byte offsets [Begin, End) into the original input string.
struct SourceRange {
  unsigned Begin = 0;
  unsigned End   = 0;
};

// ---------------------------------------------------------------------------
// Kind / KeyKind enumerations
// ---------------------------------------------------------------------------

/// Discriminator for Value nodes.
enum class Kind {
  // Standard JSON
  Null,
  Boolean,
  Integer,
  Float,
  String,
  Array,
  Object,
  // Pattern extensions
  Wildcard,      // &
  Capture,       // &<ident>  — payload = identifier name
  Regex,         // #"pat"    — payload = decoded pattern string
  Substitution,  // <<name>>  — payload = variable name
};

/// Discriminator for object key nodes.
enum class KeyKind {
  Regular,      // "key"         — colon + value follow
  Absent,       // !"key"        — no colon or value
  Wildcard,     // &             — colon + value follow; payload empty
  Capture,      // &<ident>      — colon + value follow; payload = name
  Regex,        // #"pattern"    — colon + value follow; payload = pattern
  Substitution, // <<name>>      — colon + value follow; payload = name
};

// ---------------------------------------------------------------------------
// ObjectKey
// ---------------------------------------------------------------------------

/// An object key token.
struct ObjectKey {
  KeyKind     Kind  = KeyKind::Regular;
  /// Decoded payload: string for Regular/Absent, identifier for Capture,
  /// decoded pattern for Regex, variable name for Substitution.
  /// Empty for Wildcard.
  std::string Value;
  /// Source range of the full key token (including prefix char and delimiters).
  SourceRange Range;
};

// ---------------------------------------------------------------------------
// Forward declarations required for the recursive AST
// ---------------------------------------------------------------------------

class Value;
struct Member;

// ---------------------------------------------------------------------------
// Array / Object container classes
//
// Defined as thin wrappers around std::vector so their sizeof() is known
// before Value is complete (std::vector stores elements on the heap).
// Their destructors are declared out-of-line and defined in JsonParser.cpp,
// where Value is complete, to break the circular definition cycle.
// ---------------------------------------------------------------------------

/// Ordered sequence of Value nodes.
class Array {
  std::vector<Value> Vals;

public:
  Array() = default;
  Array(Array &&) = default;
  Array &operator=(Array &&) = default;
  ~Array(); // defined in .cpp

  using iterator       = std::vector<Value>::iterator;
  using const_iterator = std::vector<Value>::const_iterator;

  iterator       begin();
  const_iterator begin() const;
  iterator       end();
  const_iterator end() const;
  bool           empty() const;
  std::size_t    size()  const;
  Value         &operator[](std::size_t I);
  const Value   &operator[](std::size_t I) const;
  void           push_back(Value V);
};

/// Ordered sequence of Member nodes.
class Object {
  std::vector<Member> Mems;

public:
  Object() = default;
  Object(Object &&) = default;
  Object &operator=(Object &&) = default;
  ~Object() = default;

  using iterator       = std::vector<Member>::iterator;
  using const_iterator = std::vector<Member>::const_iterator;

  iterator       begin();
  const_iterator begin() const;
  iterator       end();
  const_iterator end() const;
  bool           empty() const;
  std::size_t    size()  const;
  Member        &operator[](std::size_t I);
  const Member  &operator[](std::size_t I) const;
  void           push_back(Member M);
};

// ---------------------------------------------------------------------------
// Member
// ---------------------------------------------------------------------------

/// An object member: a key plus an optional value.
/// Val is nullptr when Key.Kind == Absent.
struct Member {
  ObjectKey              Key;
  std::unique_ptr<Value> Val; // nullptr iff Key.Kind == Absent

  Member() = default;
  Member(Member &&) = default;
  Member &operator=(Member &&) = default;
  ~Member(); // defined in .cpp
};

// ---------------------------------------------------------------------------
// ParseError
// ---------------------------------------------------------------------------

/// Structured parse-error information.
struct ParseError {
  std::string Message;
  unsigned    Offset = 0; ///< Byte offset into the input.
  unsigned    Line   = 1; ///< 1-indexed line number.
  unsigned    Column = 1; ///< 1-indexed column (bytes from line start).
};

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

/// A parsed JsonCheck value node.
///
/// Values are move-only (Array/Object nodes are heap-allocated).
/// Use the static factory methods or jsoncheck::parse() to create Values.
class Value {
public:
  // Move-only
  Value(Value &&Other) noexcept;
  Value &operator=(Value &&Other) noexcept;
  Value(const Value &)            = delete;
  Value &operator=(const Value &) = delete;
  ~Value(); // defined in .cpp

  Kind        getKind()  const { return K; }
  SourceRange getRange() const { return Range; }

  bool isNull()     const { return K == Kind::Null; }
  bool isWildcard() const { return K == Kind::Wildcard; }

  std::optional<bool>        getAsBoolean() const;
  std::optional<int64_t>     getAsInteger() const;
  std::optional<double>      getAsFloat()   const;
  /// Returns the string payload for String/Capture/Regex/Substitution.
  std::optional<StringRef>   getAsString()  const;

  const Array  *getAsArray()  const;
        Array  *getAsArray();
  const Object *getAsObject() const;
        Object *getAsObject();

  // ---- Factory methods (public so the internal Parser can call them) ----
  static Value makeNull(SourceRange R);
  static Value makeBoolean(bool B, SourceRange R);
  static Value makeInteger(int64_t I, SourceRange R);
  static Value makeFloat(double D, SourceRange R);
  static Value makeString(std::string S, SourceRange R);
  static Value makeArray(Array A, SourceRange R);
  static Value makeObject(Object O, SourceRange R);
  static Value makeWildcard(SourceRange R);
  static Value makeCapture(std::string Name, SourceRange R);
  static Value makeRegex(std::string Pattern, SourceRange R);
  static Value makeSubstitution(std::string Name, SourceRange R);

private:
  using Storage = std::variant<
    std::monostate,           // Null or Wildcard (no payload)
    bool,                     // Boolean
    int64_t,                  // Integer
    double,                   // Float
    std::string,              // String / Capture / Regex / Substitution
    std::unique_ptr<Array>,   // Array
    std::unique_ptr<Object>   // Object
  >;

  explicit Value(Kind K, SourceRange R, Storage S)
      : K(K), Range(R), Stor(std::move(S)) {}

  Kind        K     = Kind::Null;
  SourceRange Range = {};
  Storage     Stor;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Parse JsonCheck superset format from \p Input.
/// Returns an error with line/column information on failure.
llvm::Expected<Value> parse(llvm::StringRef Input);

/// Convert a byte offset into a (line, column) pair (both 1-indexed).
std::pair<unsigned, unsigned> offsetToLineCol(llvm::StringRef Input,
                                              unsigned Offset);

} // namespace jsoncheck
} // namespace llvm

#endif // LLVM_JSONCHECK_JSONPARSER_H
