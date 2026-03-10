//===--- JsonParser.cpp - JsonCheck pattern parser ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/JsonCheck/JsonParser.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>

using namespace llvm;
using namespace llvm::jsoncheck;

//===----------------------------------------------------------------------===//
// Out-of-line destructors (defined here where Value is complete)
//===----------------------------------------------------------------------===//

Array::~Array()  = default;
Member::~Member() = default;

//===----------------------------------------------------------------------===//
// Array methods
//===----------------------------------------------------------------------===//

Array::iterator       Array::begin()        { return Vals.begin(); }
Array::const_iterator Array::begin()  const { return Vals.begin(); }
Array::iterator       Array::end()          { return Vals.end();   }
Array::const_iterator Array::end()    const { return Vals.end();   }
bool                  Array::empty()  const { return Vals.empty(); }
std::size_t           Array::size()   const { return Vals.size();  }

Value       &Array::operator[](std::size_t I)       { return Vals[I]; }
const Value &Array::operator[](std::size_t I) const { return Vals[I]; }

void Array::push_back(Value V) { Vals.push_back(std::move(V)); }

//===----------------------------------------------------------------------===//
// Object methods
//===----------------------------------------------------------------------===//

Object::iterator       Object::begin()        { return Mems.begin(); }
Object::const_iterator Object::begin()  const { return Mems.begin(); }
Object::iterator       Object::end()          { return Mems.end();   }
Object::const_iterator Object::end()    const { return Mems.end();   }
bool                   Object::empty()  const { return Mems.empty(); }
std::size_t            Object::size()   const { return Mems.size();  }

Member       &Object::operator[](std::size_t I)       { return Mems[I]; }
const Member &Object::operator[](std::size_t I) const { return Mems[I]; }

void Object::push_back(Member M) { Mems.push_back(std::move(M)); }

//===----------------------------------------------------------------------===//
// Value: special members
//===----------------------------------------------------------------------===//

Value::Value(Value &&Other) noexcept
    : K(Other.K), Range(Other.Range), Stor(std::move(Other.Stor)) {
  Other.K     = Kind::Null;
  Other.Range = {};
}

Value &Value::operator=(Value &&Other) noexcept {
  if (this != &Other) {
    K           = Other.K;
    Range       = Other.Range;
    Stor        = std::move(Other.Stor);
    Other.K     = Kind::Null;
    Other.Range = {};
  }
  return *this;
}

Value::~Value() = default;

//===----------------------------------------------------------------------===//
// Value: accessors
//===----------------------------------------------------------------------===//

std::optional<bool> Value::getAsBoolean() const {
  if (K == Kind::Boolean)
    return std::get<bool>(Stor);
  return std::nullopt;
}

std::optional<int64_t> Value::getAsInteger() const {
  if (K == Kind::Integer)
    return std::get<int64_t>(Stor);
  return std::nullopt;
}

std::optional<double> Value::getAsFloat() const {
  if (K == Kind::Float)
    return std::get<double>(Stor);
  return std::nullopt;
}

std::optional<StringRef> Value::getAsString() const {
  if (K == Kind::String || K == Kind::Capture ||
      K == Kind::Regex   || K == Kind::Substitution)
    return StringRef(std::get<std::string>(Stor));
  return std::nullopt;
}

const Array *Value::getAsArray() const {
  if (K == Kind::Array)
    return std::get<std::unique_ptr<Array>>(Stor).get();
  return nullptr;
}

Array *Value::getAsArray() {
  if (K == Kind::Array)
    return std::get<std::unique_ptr<Array>>(Stor).get();
  return nullptr;
}

const Object *Value::getAsObject() const {
  if (K == Kind::Object)
    return std::get<std::unique_ptr<Object>>(Stor).get();
  return nullptr;
}

Object *Value::getAsObject() {
  if (K == Kind::Object)
    return std::get<std::unique_ptr<Object>>(Stor).get();
  return nullptr;
}

//===----------------------------------------------------------------------===//
// Value: factory methods
//===----------------------------------------------------------------------===//

Value Value::makeNull(SourceRange R) {
  return Value(Kind::Null, R, std::monostate{});
}

Value Value::makeBoolean(bool B, SourceRange R) {
  return Value(Kind::Boolean, R, B);
}

Value Value::makeInteger(int64_t I, SourceRange R) {
  return Value(Kind::Integer, R, I);
}

Value Value::makeFloat(double D, SourceRange R) {
  return Value(Kind::Float, R, D);
}

Value Value::makeString(std::string S, SourceRange R) {
  return Value(Kind::String, R, std::move(S));
}

Value Value::makeArray(Array A, SourceRange R) {
  return Value(Kind::Array, R,
               std::make_unique<Array>(std::move(A)));
}

Value Value::makeObject(Object O, SourceRange R) {
  return Value(Kind::Object, R,
               std::make_unique<Object>(std::move(O)));
}

Value Value::makeWildcard(SourceRange R) {
  return Value(Kind::Wildcard, R, std::monostate{});
}

Value Value::makeCapture(std::string Name, SourceRange R) {
  return Value(Kind::Capture, R, std::move(Name));
}

Value Value::makeRegex(std::string Pattern, SourceRange R) {
  return Value(Kind::Regex, R, std::move(Pattern));
}

Value Value::makeSubstitution(std::string Name, SourceRange R) {
  return Value(Kind::Substitution, R, std::move(Name));
}

//===----------------------------------------------------------------------===//
// offsetToLineCol
//===----------------------------------------------------------------------===//

std::pair<unsigned, unsigned>
jsoncheck::offsetToLineCol(StringRef Input, unsigned Offset) {
  unsigned Line = 1, Col = 1;
  for (unsigned I = 0; I < Offset && I < Input.size(); ++I) {
    if (Input[I] == '\n') {
      ++Line;
      Col = 1;
    } else {
      ++Col;
    }
  }
  return {Line, Col};
}

//===----------------------------------------------------------------------===//
// Internal recursive-descent parser
//===----------------------------------------------------------------------===//

namespace {

static bool isIdentStart(char C) {
  return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_';
}

static bool isIdentContinue(char C) {
  return isIdentStart(C) || (C >= '0' && C <= '9');
}

static bool isJsonNumberChar(char C) {
  return (C >= '0' && C <= '9') || C == 'e' || C == 'E' ||
         C == '+' || C == '-' || C == '.';
}

class Parser {
public:
  Parser(StringRef Input)
      : Start(Input.begin()), P(Input.begin()), End(Input.end()) {}

  bool parseValue(Value &Out);

  bool assertEnd() {
    eatWhitespace();
    if (Err)
      return false;
    if (P == End)
      return true;
    return error("Text after end of document");
  }

  Error takeError() {
    assert(Err && "takeError called without an error");
    auto &E = *Err;
    return createStringError(inconvertibleErrorCode(),
                             "%s (line %u, column %u, offset %u)",
                             E.Message.c_str(), E.Line, E.Column, E.Offset);
  }

private:
  const char *Start;
  const char *P;
  const char *End;
  std::optional<ParseError> Err;

  unsigned offset() const { return unsigned(P - Start); }

  char next() { return P == End ? '\0' : *P++; }
  char peek() const { return P == End ? '\0' : *P; }

  // ---- Whitespace and comments ----------------------------------------

  void eatWhitespace() {
    while (P != End && !Err) {
      char C = *P;
      if (C == ' ' || C == '\r' || C == '\n' || C == '\t') {
        ++P;
      } else if (P + 1 < End && C == '/' && *(P + 1) == '/') {
        // Line comment: skip to end of line.
        P += 2;
        while (P != End && *P != '\n')
          ++P;
      } else if (P + 1 < End && C == '/' && *(P + 1) == '*') {
        // Block comment: skip until closing */.
        P += 2;
        while (P + 1 < End && !(*P == '*' && *(P + 1) == '/'))
          ++P;
        if (P + 1 >= End) {
          error("Unterminated block comment");
          return;
        }
        P += 2; // skip */
      } else {
        break;
      }
    }
  }

  // ---- Error -----------------------------------------------------------

  bool error(const char *Msg) {
    unsigned Off = offset();
    auto [Line, Col] = offsetToLineCol(StringRef(Start, End - Start), Off);
    Err = ParseError{Msg, Off, Line, Col};
    return false;
  }

  // ---- Unicode escape helper ------------------------------------------

  bool parseUnicode(std::string &Out) {
    // Reuse the same surrogate-handling logic as llvm::json.
    auto Invalid = [&] { Out.append({'\xef', '\xbf', '\xbd'}); };

    auto Parse4Hex = [&](uint16_t &H) -> bool {
      H = 0;
      char Bytes[4] = {next(), next(), next(), next()};
      for (unsigned char B : Bytes) {
        if (!std::isxdigit(B))
          return error("Invalid \\u escape sequence");
        H <<= 4;
        H |= (B > '9') ? ((B & ~0x20) - 'A' + 10) : (B - '0');
      }
      return true;
    };

    auto EncodeUtf8 = [](uint32_t R, std::string &S) {
      if (R < 0x80) {
        S.push_back(char(R));
      } else if (R < 0x800) {
        S.push_back(char(0xC0 | ((R >> 6) & 0x1F)));
        S.push_back(char(0x80 | (R & 0x3F)));
      } else if (R < 0x10000) {
        S.push_back(char(0xE0 | ((R >> 12) & 0x0F)));
        S.push_back(char(0x80 | ((R >> 6) & 0x3F)));
        S.push_back(char(0x80 | (R & 0x3F)));
      } else {
        S.push_back(char(0xF0 | ((R >> 18) & 0x07)));
        S.push_back(char(0x80 | ((R >> 12) & 0x3F)));
        S.push_back(char(0x80 | ((R >> 6) & 0x3F)));
        S.push_back(char(0x80 | (R & 0x3F)));
      }
    };

    uint16_t First;
    if (!Parse4Hex(First))
      return false;

    while (true) {
      if (First < 0xD800 || First >= 0xE000) {
        EncodeUtf8(First, Out);
        return true;
      }
      if (First >= 0xDC00) {
        Invalid();
        return true;
      }
      // Leading surrogate: expect a trailing \u escape.
      if (P + 2 > End || *P != '\\' || *(P + 1) != 'u') {
        Invalid();
        return true;
      }
      P += 2;
      uint16_t Second;
      if (!Parse4Hex(Second))
        return false;
      if (Second < 0xDC00 || Second >= 0xE000) {
        Invalid();
        First = Second;
        continue;
      }
      EncodeUtf8(0x10000 | ((First - 0xD800) << 10) | (Second - 0xDC00), Out);
      return true;
    }
  }

  // ---- String content (opening " already consumed) ---------------------

  bool parseStringContent(std::string &Out) {
    for (;;) {
      if (P == End)
        return error("Unterminated string");
      char C = next();
      if (C == '"')
        return true;
      if ((C & 0x1f) == C)
        return error("Control character in string");
      if (C != '\\') {
        Out.push_back(C);
        continue;
      }
      // Escape sequence.
      switch (C = next()) {
      case '"': case '\\': case '/': Out.push_back(C); break;
      case 'b': Out.push_back('\b'); break;
      case 'f': Out.push_back('\f'); break;
      case 'n': Out.push_back('\n'); break;
      case 'r': Out.push_back('\r'); break;
      case 't': Out.push_back('\t'); break;
      case 'u':
        if (!parseUnicode(Out))
          return false;
        break;
      default:
        return error("Invalid escape sequence");
      }
    }
  }

  // ---- Number ----------------------------------------------------------

  bool parseNumber(char First, Value &Out, unsigned Begin) {
    SmallString<24> S;
    S.push_back(First);
    while (isJsonNumberChar(peek()))
      S.push_back(next());

    char *Ep;
    errno = 0;
    int64_t I = std::strtoll(S.c_str(), &Ep, 10);
    if (Ep == S.end() && errno != ERANGE) {
      Out = Value::makeInteger(I, {Begin, offset()});
      return true;
    }
    if (First != '-') {
      errno = 0;
      uint64_t U = std::strtoull(S.c_str(), &Ep, 10);
      if (Ep == S.end() && errno != ERANGE) {
        // Store as int64 if it fits, otherwise fall through to float.
        if (U <= uint64_t(std::numeric_limits<int64_t>::max())) {
          Out = Value::makeInteger(int64_t(U), {Begin, offset()});
          return true;
        }
      }
    }
    double D = std::strtod(S.c_str(), &Ep);
    if (Ep != S.end())
      return error("Invalid JSON number");
    Out = Value::makeFloat(D, {Begin, offset()});
    return true;
  }

  // ---- Pattern: capture or wildcard (value position, '&' consumed) -----

  bool parseCapture(Value &Out, unsigned Begin) {
    if (isIdentStart(peek())) {
      std::string Name;
      while (isIdentContinue(peek()))
        Name.push_back(next());
      Out = Value::makeCapture(std::move(Name), {Begin, offset()});
    } else {
      Out = Value::makeWildcard({Begin, offset()});
    }
    return true;
  }

  // ---- Pattern: regex (value position, '#' consumed) -------------------

  bool parseRegex(Value &Out, unsigned Begin) {
    if (peek() != '"')
      return error("Expected '\"' after '#' for regex pattern");
    next(); // consume '"'
    std::string Pat;
    if (!parseStringContent(Pat))
      return false;
    Out = Value::makeRegex(std::move(Pat), {Begin, offset()});
    return true;
  }

  // ---- Pattern: substitution (value position, first '<' consumed) ------

  bool parseSubstitution(Value &Out, unsigned Begin) {
    if (peek() != '<')
      return error("Expected '<<' for substitution");
    next(); // consume second '<'
    std::string Name;
    while (P != End && *P != '>' && *P != '\n')
      Name.push_back(next());
    if (P + 1 >= End || *P != '>' || *(P + 1) != '>')
      return error("Unterminated substitution (expected '>>')");
    P += 2; // consume >>
    if (Name.empty())
      return error("Empty substitution name");
    Out = Value::makeSubstitution(std::move(Name), {Begin, offset()});
    return true;
  }

  // ---- Array -----------------------------------------------------------

  bool parseArray(Value &Out, unsigned Begin) {
    Array A;
    eatWhitespace();
    if (Err) return false;
    if (peek() == ']') {
      next();
      Out = Value::makeArray(std::move(A), {Begin, offset()});
      return true;
    }
    for (;;) {
      Value Elem(Value::makeNull({}));
      if (!parseValue(Elem))
        return false;
      A.push_back(std::move(Elem));
      eatWhitespace();
      if (Err) return false;
      char C = next();
      if (C == ']')
        break;
      if (C != ',')
        return error("Expected ',' or ']' after array element");
      eatWhitespace();
      if (Err) return false;
      // Allow trailing comma.
      if (peek() == ']') {
        next();
        break;
      }
    }
    Out = Value::makeArray(std::move(A), {Begin, offset()});
    return true;
  }

  // ---- Object key: regular string key ('"' already consumed) -----------

  bool parseRegularKey(ObjectKey &Key, unsigned KeyBegin) {
    std::string S;
    if (!parseStringContent(S))
      return false;
    Key.Kind  = KeyKind::Regular;
    Key.Value = std::move(S);
    Key.Range = {KeyBegin, offset()};
    return true;
  }

  // ---- Object key: absence assertion ('!' already consumed) ------------

  bool parseAbsenceKey(ObjectKey &Key, unsigned KeyBegin) {
    if (peek() != '"')
      return error("Expected '\"' after '!' for absence assertion");
    next(); // consume '"'
    std::string S;
    if (!parseStringContent(S))
      return false;
    Key.Kind  = KeyKind::Absent;
    Key.Value = std::move(S);
    Key.Range = {KeyBegin, offset()};
    return true;
  }

  // ---- Object key: wildcard/capture ('&' already consumed) -------------

  bool parseKeyCapture(ObjectKey &Key, unsigned KeyBegin) {
    if (isIdentStart(peek())) {
      std::string Name;
      while (isIdentContinue(peek()))
        Name.push_back(next());
      Key.Kind  = KeyKind::Capture;
      Key.Value = std::move(Name);
    } else {
      Key.Kind  = KeyKind::Wildcard;
      Key.Value = "";
    }
    Key.Range = {KeyBegin, offset()};
    return true;
  }

  // ---- Object key: regex ('#' already consumed) ------------------------

  bool parseKeyRegex(ObjectKey &Key, unsigned KeyBegin) {
    if (peek() != '"')
      return error("Expected '\"' after '#' for key regex");
    next(); // consume '"'
    std::string Pat;
    if (!parseStringContent(Pat))
      return false;
    Key.Kind  = KeyKind::Regex;
    Key.Value = std::move(Pat);
    Key.Range = {KeyBegin, offset()};
    return true;
  }

  // ---- Object key: substitution (first '<' already consumed) ----------

  bool parseKeySubstitution(ObjectKey &Key, unsigned KeyBegin) {
    if (peek() != '<')
      return error("Expected '<<' for key substitution");
    next(); // consume second '<'
    std::string Name;
    while (P != End && *P != '>' && *P != '\n')
      Name.push_back(next());
    if (P + 1 >= End || *P != '>' || *(P + 1) != '>')
      return error("Unterminated substitution in key (expected '>>')");
    P += 2;
    if (Name.empty())
      return error("Empty substitution name in key");
    Key.Kind  = KeyKind::Substitution;
    Key.Value = std::move(Name);
    Key.Range = {KeyBegin, offset()};
    return true;
  }

  // ---- Object member ---------------------------------------------------

  bool parseMember(Member &Out, const Object &CurrentObj) {
    eatWhitespace();
    if (Err) return false;
    unsigned KeyBegin = offset();
    char C = next();

    ObjectKey Key;
    bool Absent = false;

    switch (C) {
    case '"':
      if (!parseRegularKey(Key, KeyBegin))
        return false;
      break;
    case '!':
      if (!parseAbsenceKey(Key, KeyBegin))
        return false;
      Absent = true;
      break;
    case '&':
      if (!parseKeyCapture(Key, KeyBegin))
        return false;
      break;
    case '#':
      if (!parseKeyRegex(Key, KeyBegin))
        return false;
      break;
    case '<':
      if (!parseKeySubstitution(Key, KeyBegin))
        return false;
      break;
    default:
      return error("Expected object key");
    }

    // Check for duplicate regular string keys.
    if (Key.Kind == KeyKind::Regular) {
      for (const Member &M : CurrentObj) {
        if (M.Key.Kind == KeyKind::Regular && M.Key.Value == Key.Value)
          return error("Duplicate object key");
      }
    }

    Out.Key = std::move(Key);

    if (Absent) {
      // Absence assertion: no colon or value.
      Out.Val = nullptr;
      return true;
    }

    // Expect ':'.
    eatWhitespace();
    if (Err) return false;
    if (next() != ':')
      return error("Expected ':' after object key");

    // Parse value.
    Value V(Value::makeNull({}));
    if (!parseValue(V))
      return false;
    Out.Val = std::make_unique<Value>(std::move(V));
    return true;
  }

  // ---- Object ----------------------------------------------------------

  bool parseObject(Value &Out, unsigned Begin) {
    Object O;
    eatWhitespace();
    if (Err) return false;
    if (peek() == '}') {
      next();
      Out = Value::makeObject(std::move(O), {Begin, offset()});
      return true;
    }
    for (;;) {
      Member M;
      if (!parseMember(M, O))
        return false;
      O.push_back(std::move(M));
      eatWhitespace();
      if (Err) return false;
      char C = next();
      if (C == '}')
        break;
      if (C != ',')
        return error("Expected ',' or '}' after object member");
      eatWhitespace();
      if (Err) return false;
      // Allow trailing comma.
      if (peek() == '}') {
        next();
        break;
      }
    }
    Out = Value::makeObject(std::move(O), {Begin, offset()});
    return true;
  }

  // ---- Top-level value parser ------------------------------------------

public:
  // (declared above; defined here for readability)
};

bool Parser::parseValue(Value &Out) {
  eatWhitespace();
  if (Err)
    return false;
  if (P == End)
    return error("Unexpected end of input");

  unsigned Begin = offset();
  char C = next();

  switch (C) {
  case 'n':
    if (next() == 'u' && next() == 'l' && next() == 'l') {
      Out = Value::makeNull({Begin, offset()});
      return true;
    }
    return error("Invalid JSON value (null?)");

  case 't':
    if (next() == 'r' && next() == 'u' && next() == 'e') {
      Out = Value::makeBoolean(true, {Begin, offset()});
      return true;
    }
    return error("Invalid JSON value (true?)");

  case 'f':
    if (next() == 'a' && next() == 'l' && next() == 's' && next() == 'e') {
      Out = Value::makeBoolean(false, {Begin, offset()});
      return true;
    }
    return error("Invalid JSON value (false?)");

  case '"': {
    std::string S;
    if (!parseStringContent(S))
      return false;
    Out = Value::makeString(std::move(S), {Begin, offset()});
    return true;
  }

  case '[':
    return parseArray(Out, Begin);

  case '{':
    return parseObject(Out, Begin);

  case '&':
    return parseCapture(Out, Begin);

  case '#':
    return parseRegex(Out, Begin);

  case '<':
    return parseSubstitution(Out, Begin);

  default:
    if (C == '-' || (C >= '0' && C <= '9'))
      return parseNumber(C, Out, Begin);
    return error("Invalid JSON value");
  }
}

} // anonymous namespace

//===----------------------------------------------------------------------===//
// Public parse() entry point
//===----------------------------------------------------------------------===//

llvm::Expected<Value> jsoncheck::parse(StringRef Input) {
  Parser P(Input);
  Value V(Value::makeNull({}));
  if (!P.parseValue(V) || !P.assertEnd())
    return P.takeError();
  return std::move(V);
}
