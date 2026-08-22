// A small JSON reader/writer, enough for the licence protocol and nothing
// more. Vendoring a full JSON library for four flat objects would cost more
// build time and audit surface than it saves.
//
// Deliberately limited:
//   - parses into a tree of Value (null/bool/number/string/array/object)
//   - numbers are read as double and exposed as int64 where we need them,
//     which is exact for the unix timestamps we deal with (< 2^53)
//   - rejects anything malformed rather than guessing; every caller here
//     treats a parse failure as "no licence", which fails closed
//   - depth-limited, so a hostile file cannot blow the stack

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eni {
namespace json {

class Value
{
public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Value() = default;
  explicit Value(bool b) : mType(Type::kBool), mBool(b) {}
  explicit Value(double n) : mType(Type::kNumber), mNumber(n) {}
  explicit Value(int64_t n) : mType(Type::kNumber), mNumber(double(n)) {}
  explicit Value(std::string s) : mType(Type::kString), mString(std::move(s)) {}

  static Value Object() { Value v; v.mType = Type::kObject; return v; }
  static Value Array() { Value v; v.mType = Type::kArray; return v; }

  Type GetType() const { return mType; }
  bool IsNull() const { return mType == Type::kNull; }
  bool IsObject() const { return mType == Type::kObject; }

  // Lookups never throw: a missing key or a type mismatch yields the
  // fallback, which is what every call site wants.
  const Value* Find(const std::string& key) const;
  std::string GetString(const std::string& key, const std::string& fallback = std::string()) const;
  int64_t GetInt(const std::string& key, int64_t fallback = 0) const;
  bool Has(const std::string& key) const { return Find(key) != nullptr; }

  std::string AsString() const { return mType == Type::kString ? mString : std::string(); }
  int64_t AsInt() const { return mType == Type::kNumber ? int64_t(mNumber) : 0; }

  void Set(const std::string& key, Value v);
  void Push(Value v);

  const std::map<std::string, Value>& Members() const { return mObject; }
  const std::vector<Value>& Items() const { return mArray; }

  std::string Dump(int indent = 0) const;

private:
  Type mType = Type::kNull;
  bool mBool = false;
  double mNumber = 0.0;
  std::string mString;
  std::vector<Value> mArray;
  std::map<std::string, Value> mObject;
};

// Returns false and leaves `out` untouched on any malformed input.
bool Parse(const std::string& text, Value& out);

// Percent-encodes for application/x-www-form-urlencoded bodies (the Auth0
// device endpoints take forms, not JSON).
std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields);

} // namespace json
} // namespace eni
