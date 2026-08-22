#include "eni_json.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace eni {
namespace json {

namespace {

// A licence file is three levels deep; anything beyond this is either a bug
// or someone probing for a stack overflow.
constexpr int kMaxDepth = 32;

class Parser
{
public:
  explicit Parser(const std::string& text) : mText(text) {}

  bool Run(Value& out)
  {
    SkipWhitespace();
    Value v;
    if (!ParseValue(v, 0)) return false;
    SkipWhitespace();
    if (mPos != mText.size()) return false; // trailing garbage
    out = std::move(v);
    return true;
  }

private:
  const std::string& mText;
  size_t mPos = 0;

  bool AtEnd() const { return mPos >= mText.size(); }
  char Peek() const { return mText[mPos]; }

  void SkipWhitespace()
  {
    while (!AtEnd())
    {
      const char c = Peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') mPos++;
      else break;
    }
  }

  bool Literal(const char* word)
  {
    const size_t len = strlen(word);
    if (mText.compare(mPos, len, word) != 0) return false;
    mPos += len;
    return true;
  }

  bool ParseValue(Value& out, int depth)
  {
    if (depth > kMaxDepth || AtEnd()) return false;

    switch (Peek())
    {
      case '{': return ParseObject(out, depth);
      case '[': return ParseArray(out, depth);
      case '"':
      {
        std::string s;
        if (!ParseString(s)) return false;
        out = Value(std::move(s));
        return true;
      }
      case 't':
        if (!Literal("true")) return false;
        out = Value(true);
        return true;
      case 'f':
        if (!Literal("false")) return false;
        out = Value(false);
        return true;
      case 'n':
        if (!Literal("null")) return false;
        out = Value();
        return true;
      default: return ParseNumber(out);
    }
  }

  bool ParseObject(Value& out, int depth)
  {
    mPos++; // {
    Value obj = Value::Object();
    SkipWhitespace();
    if (!AtEnd() && Peek() == '}') { mPos++; out = std::move(obj); return true; }

    for (;;)
    {
      SkipWhitespace();
      std::string key;
      if (AtEnd() || Peek() != '"' || !ParseString(key)) return false;
      SkipWhitespace();
      if (AtEnd() || Peek() != ':') return false;
      mPos++;
      SkipWhitespace();
      Value v;
      if (!ParseValue(v, depth + 1)) return false;
      obj.Set(key, std::move(v));
      SkipWhitespace();
      if (AtEnd()) return false;
      if (Peek() == ',') { mPos++; continue; }
      if (Peek() == '}') { mPos++; break; }
      return false;
    }
    out = std::move(obj);
    return true;
  }

  bool ParseArray(Value& out, int depth)
  {
    mPos++; // [
    Value arr = Value::Array();
    SkipWhitespace();
    if (!AtEnd() && Peek() == ']') { mPos++; out = std::move(arr); return true; }

    for (;;)
    {
      SkipWhitespace();
      Value v;
      if (!ParseValue(v, depth + 1)) return false;
      arr.Push(std::move(v));
      SkipWhitespace();
      if (AtEnd()) return false;
      if (Peek() == ',') { mPos++; continue; }
      if (Peek() == ']') { mPos++; break; }
      return false;
    }
    out = std::move(arr);
    return true;
  }

  // Appends a code point as UTF-8. Surrogate pairs are joined by the caller.
  static void AppendUtf8(std::string& out, uint32_t cp)
  {
    if (cp < 0x80) { out += char(cp); }
    else if (cp < 0x800)
    {
      out += char(0xC0 | (cp >> 6));
      out += char(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
      out += char(0xE0 | (cp >> 12));
      out += char(0x80 | ((cp >> 6) & 0x3F));
      out += char(0x80 | (cp & 0x3F));
    }
    else
    {
      out += char(0xF0 | (cp >> 18));
      out += char(0x80 | ((cp >> 12) & 0x3F));
      out += char(0x80 | ((cp >> 6) & 0x3F));
      out += char(0x80 | (cp & 0x3F));
    }
  }

  bool ParseHex4(uint32_t& out)
  {
    if (mPos + 4 > mText.size()) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
    {
      const char c = mText[mPos + i];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
      else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
      else return false;
    }
    mPos += 4;
    out = v;
    return true;
  }

  bool ParseString(std::string& out)
  {
    mPos++; // opening quote
    std::string s;
    while (!AtEnd())
    {
      const unsigned char c = static_cast<unsigned char>(mText[mPos]);
      if (c == '"') { mPos++; out = std::move(s); return true; }
      if (c < 0x20) return false; // raw control characters are not legal
      if (c != '\\') { s += char(c); mPos++; continue; }

      mPos++;
      if (AtEnd()) return false;
      const char esc = mText[mPos++];
      switch (esc)
      {
        case '"': s += '"'; break;
        case '\\': s += '\\'; break;
        case '/': s += '/'; break;
        case 'b': s += '\b'; break;
        case 'f': s += '\f'; break;
        case 'n': s += '\n'; break;
        case 'r': s += '\r'; break;
        case 't': s += '\t'; break;
        case 'u':
        {
          uint32_t cp = 0;
          if (!ParseHex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) // high surrogate: expect the low one
          {
            if (mPos + 1 < mText.size() && mText[mPos] == '\\' && mText[mPos + 1] == 'u')
            {
              mPos += 2;
              uint32_t lo = 0;
              if (!ParseHex4(lo)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF) return false;
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            else return false;
          }
          else if (cp >= 0xDC00 && cp <= 0xDFFF) return false; // lone low surrogate
          AppendUtf8(s, cp);
          break;
        }
        default: return false;
      }
    }
    return false; // unterminated
  }

  bool ParseNumber(Value& out)
  {
    const size_t start = mPos;
    if (!AtEnd() && Peek() == '-') mPos++;

    // JSON forbids leading zeros ("01"), and so do we: silently accepting
    // them would mean this parser and the server's disagree about what a
    // given byte sequence says.
    bool digits = false;
    if (!AtEnd() && Peek() == '0')
    {
      mPos++;
      digits = true;
      if (!AtEnd() && Peek() >= '0' && Peek() <= '9') return false;
    }
    else
    {
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9') { mPos++; digits = true; }
    }
    if (!AtEnd() && Peek() == '.')
    {
      mPos++;
      bool frac = false;
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9') { mPos++; frac = true; }
      if (!frac) return false;
    }
    if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
    {
      mPos++;
      if (!AtEnd() && (Peek() == '+' || Peek() == '-')) mPos++;
      bool exp = false;
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9') { mPos++; exp = true; }
      if (!exp) return false;
    }
    if (!digits) return false;

    const std::string text = mText.substr(start, mPos - start);
    out = Value(std::strtod(text.c_str(), nullptr));
    return true;
  }
};

void DumpString(const std::string& s, std::string& out)
{
  out += '"';
  for (const char raw : s)
  {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c)
    {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20)
        {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        }
        else out += char(c); // UTF-8 passes through untouched
    }
  }
  out += '"';
}

} // namespace

const Value* Value::Find(const std::string& key) const
{
  if (mType != Type::kObject) return nullptr;
  const auto it = mObject.find(key);
  return it == mObject.end() ? nullptr : &it->second;
}

std::string Value::GetString(const std::string& key, const std::string& fallback) const
{
  const Value* v = Find(key);
  return (v && v->mType == Type::kString) ? v->mString : fallback;
}

int64_t Value::GetInt(const std::string& key, int64_t fallback) const
{
  const Value* v = Find(key);
  if (!v || v->mType != Type::kNumber) return fallback;
  // Reject NaN/inf and anything that lost precision as an integer, so a
  // crafted "exp": 1e400 cannot turn into a licence that never expires.
  if (!std::isfinite(v->mNumber)) return fallback;
  if (v->mNumber > 9.0e15 || v->mNumber < -9.0e15) return fallback;
  return int64_t(v->mNumber);
}

void Value::Set(const std::string& key, Value v)
{
  if (mType != Type::kObject) { mType = Type::kObject; mObject.clear(); }
  mObject[key] = std::move(v);
}

void Value::Push(Value v)
{
  if (mType != Type::kArray) { mType = Type::kArray; mArray.clear(); }
  mArray.push_back(std::move(v));
}

std::string Value::Dump(int indent) const
{
  // indent < 0 means "no whitespace at all" (request bodies). Build the
  // padding only in that case: size_t(-1) * 2 would ask for a string of
  // 2^64 spaces.
  const bool pretty = indent >= 0;
  const std::string pad(pretty ? size_t(indent) * 2 : 0, ' ');
  const std::string padInner(pretty ? size_t(indent + 1) * 2 : 0, ' ');
  std::string out;

  switch (mType)
  {
    case Type::kNull: return "null";
    case Type::kBool: return mBool ? "true" : "false";
    case Type::kNumber:
    {
      char buf[32];
      if (mNumber == std::floor(mNumber) && std::fabs(mNumber) < 9.0e15)
        snprintf(buf, sizeof(buf), "%lld", (long long)mNumber);
      else
        snprintf(buf, sizeof(buf), "%.17g", mNumber);
      return buf;
    }
    case Type::kString: DumpString(mString, out); return out;
    case Type::kArray:
    {
      if (mArray.empty()) return "[]";
      out += '[';
      for (size_t i = 0; i < mArray.size(); i++)
      {
        if (i) out += ',';
        if (pretty) { out += '\n'; out += padInner; }
        out += mArray[i].Dump(pretty ? indent + 1 : -1);
      }
      if (pretty) { out += '\n'; out += pad; }
      out += ']';
      return out;
    }
    case Type::kObject:
    {
      if (mObject.empty()) return "{}";
      out += '{';
      bool first = true;
      for (const auto& kv : mObject)
      {
        if (!first) out += ',';
        first = false;
        if (pretty) { out += '\n'; out += padInner; }
        DumpString(kv.first, out);
        out += pretty ? ": " : ":";
        out += kv.second.Dump(pretty ? indent + 1 : -1);
      }
      if (pretty) { out += '\n'; out += pad; }
      out += '}';
      return out;
    }
  }
  return "null";
}

bool Parse(const std::string& text, Value& out)
{
  Parser p(text);
  return p.Run(out);
}

std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields)
{
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (const auto& kv : fields)
  {
    if (!out.empty()) out += '&';
    for (int part = 0; part < 2; part++)
    {
      const std::string& s = part == 0 ? kv.first : kv.second;
      for (const char raw : s)
      {
        const unsigned char c = static_cast<unsigned char>(raw);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == '~';
        if (unreserved) out += char(c);
        else { out += '%'; out += kHex[c >> 4]; out += kHex[c & 0x0F]; }
      }
      if (part == 0) out += '=';
    }
  }
  return out;
}

} // namespace json
} // namespace eni
