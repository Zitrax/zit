// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "types.hpp"

/**
 * @brief Provides encode/decode support for the bencode format
 *
 * Example use (lists):
 *
 * Encode:
 * @code {.cpp}
 *   auto v = std::vector<ElmPtr>();
 *   v.push_back(Element::build("spam"));
 *   v.push_back(Element::build("egg"));
 *   encode(v); // Returns: "l4:spam3:egge"
 * @endcode
 *
 * Decode:
 * @code {.cpp}
 *   auto v =
 * decode("l4:spam3:egge")->to<TypedElement<vector<ElmPtr>>>()->val();
 *   *v[0]->to<TypedElement<string>>(); // Returns: "spam"
 *   *v[1]->to<TypedElement<string>>(); // Returns: "egg"
 * @endcode
 */
namespace bencode {

// Forward declarations
template <typename T>
class TypedElement;
class Element;

// Typedefs
using ElmPtr = std::shared_ptr<Element>;
using BeDict = std::map<std::string, ElmPtr>;
using BeList = std::vector<ElmPtr>;

// Constants
constexpr auto MAX_LINE_WIDTH = 72;
constexpr auto MAX_STRING_LENGTH = 100'000'000;
constexpr auto MAX_INVALID_STRING_LENGTH = 128;
constexpr auto ASCII_LAST_CTRL_CHAR = 31;
constexpr auto RECURSION_LIMIT = 200;

// For stream indentation level
static const auto INDENT_INDEX = std::ios_base::xalloc();

/**
 * These two templates are used to convert arrays to pointer specifically to
 * allow a char[] to use the std::string specialization.
 */
template <class T>
struct ArrayToPointerDecay {
  using type = T;
};

/**
 * These two templates are used to convert arrays to pointer specifically to
 * allow a char[] to use the std::string specialization.
 */
template <class T, std::size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
struct ArrayToPointerDecay<T[N]> {
  using type = const T*;
};

// Function templates
template <typename T>
[[nodiscard]] std::string encode_internal(const T& in) {
  std::stringstream ss;
  ss << "i" << in << "e";
  return ss.str();
}

template <typename T>
[[nodiscard]] std::string encode(const T& in) {
  using src = ArrayToPointerDecay<T>::type;
  return encode_internal<src>(in);
}

// Exceptions

/**
 * Thrown when conversion between types happens.
 */
class BencodeConversionError : public std::runtime_error {
 public:
  explicit BencodeConversionError(const std::string& what_arg)
      : std::runtime_error(what_arg) {}
};

// Classes

/**
 * One element/value in the Bencoded file.
 *
 * Use of std::enable_shared_from_this allows safe generation of shared_ptr
 * instances that share ownership of this.
 */
class Element : public std::enable_shared_from_this<Element> {
 public:
  virtual ~Element() = default;

  // Special member functions (hicpp-special-member-functions)
  // clang needs this since we default the destructor
  Element() = default;
  Element(const Element& other) = default;
  Element(Element&& other) noexcept = default;
  Element& operator=(const Element& rhs) = default;
  Element& operator=(Element&& other) noexcept = default;

  [[nodiscard]] virtual std::string encode() const = 0;

  template <typename T>
  [[nodiscard]] auto to() const {
    static_assert(std::is_base_of_v<Element, T>,
                  "Can only return sublasses of Element");
    auto ptr = std::dynamic_pointer_cast<const T>(shared_from_this());
    if (!ptr) {
      throw BencodeConversionError("Could not convert to type");
    }
    return std::move(ptr);
  }

  template <typename T>
  [[nodiscard]] auto is() const {
    static_assert(std::is_base_of_v<Element, T>,
                  "Can only return sublasses of Element");
    return std::dynamic_pointer_cast<const T>(shared_from_this()) != nullptr;
  }

  virtual std::ostream& print(std::ostream& os) = 0;

  /**
   * First try used T&& val, but it caused storage of int references when we
   * want such values copied. See https://stackoverflow.com/q/17316386/11722 and
   * https://isocpp.org/blog/2012/11/universal-references-in-c11-scott-meyers
   * for details.
   */
  template <typename T>
  [[nodiscard]] static auto build(T val) {
    return std::make_shared<TypedElement<T>>(std::move(val));
  }

  [[nodiscard]] static auto build(const char* val) {
    return build(std::string(val));
  }
};

/**
 * Element containing any type
 */
template <typename T>
class TypedElement : public Element {
 public:
  /**
   * Make sure we can store both moved and copied data
   * See: https://stackoverflow.com/q/48862454/11722
   */
  explicit TypedElement(std::add_rvalue_reference_t<T> data)
      : m_data(std::forward<T>(data)) {}

  [[nodiscard]] std::string encode() const override {
    return bencode::encode(m_data);
  }

  [[nodiscard]] auto val() const { return m_data; }

  /**
   * Pretty print to console.
   */
  std::ostream& print(std::ostream& os) override {
    if constexpr (std::is_same<T, std::map<std::string, ElmPtr>>()) {
      auto indent = [&os]() {
        for (long i = 0; i < os.iword(INDENT_INDEX); ++i) {
          os << " ";
        }
      };

      os << "{\n";
      os.iword(INDENT_INDEX) += 2;
      auto it = m_data.cbegin();
      if (it != m_data.cend()) {
        auto elm = *it++;
        indent();
        os << elm.first << " : " << elm.second;
      }
      while (it != m_data.cend()) {
        auto elm = *it++;
        os << ",\n";
        indent();
        os << elm.first << " : " << elm.second;
      }
      os.iword(INDENT_INDEX) -= 2;
      os << "\n";
      indent();
      os << "}";
    } else if constexpr (std::is_same<T, std::vector<ElmPtr>>()) {
      os << "[";
      auto it = m_data.cbegin();
      if (it != m_data.cend()) {
        os << *it++;
      }
      while (it != m_data.cend()) {
        os << "," << *it++;
      }
      os << "]";
    } else if constexpr (std::is_same<T, std::string>()) {
      std::stringstream console_safe;
      for (const char c : m_data.substr(0, MAX_LINE_WIDTH)) {
        console_safe << (c > ASCII_LAST_CTRL_CHAR ? c : '?');
      }
      os << console_safe.str();
      if (m_data.length() > MAX_LINE_WIDTH) {
        os << " ... <" << m_data.length() << ">";
      }
    } else {
      os << m_data;
    }
    return os;
  }

  /**
   * Allow comparisons to the content type without manually calling val()
   */
  template <typename U = T>
  friend bool operator==(const TypedElement<T>& lhs, const T& rhs) {
    return lhs.m_data == rhs;
  }

 private:
  T m_data;
};

// Template specializations
template <>
[[nodiscard]] inline std::string encode_internal(const std::string& str) {
  std::stringstream ss;
  ss << str.length() << ":" << str;
  return ss.str();
}

template <>
[[nodiscard]] inline std::string encode_internal(const BeList& elist) {
  std::stringstream ss;
  ss << "l";
  for (const auto& elm : elist) {
    ss << elm->encode();
  }
  ss << "e";
  return ss.str();
}

template <>
[[nodiscard]] inline std::string encode_internal(const char* const& in) {
  return encode_internal(std::string(in));
}

template <>
[[nodiscard]] inline std::string encode_internal(const BeDict& emap) {
  std::stringstream ss;
  ss << "d";
  for (const auto& elm : emap) {
    ss << encode(elm.first) << elm.second->encode();
  }
  ss << "e";
  return ss.str();
}

[[nodiscard]] inline ElmPtr decodeInt(std::istringstream& iss) {
  int64_t i64{};
  iss >> i64;
  if (iss.fail()) {
    throw std::invalid_argument("Could not convert to integer");
  }
  if (iss.get() != 'e') {
    throw std::invalid_argument("No integer end marker");
  }
  return Element::build(i64);
}

[[nodiscard]] inline ElmPtr decodeString(std::istringstream& iss) {
  uint64_t strlen{};
  iss >> strlen;
  if (iss.fail()) {
    throw std::invalid_argument("Could not convert string length to integer");
  }
  if (iss.get() != ':') {
    throw std::invalid_argument("No string length end marker");
  }
  if (strlen > MAX_STRING_LENGTH) {
    throw std::invalid_argument("String length " + std::to_string(strlen) +
                                " larger than max size");
  }
  std::string str(zit::numeric_cast<std::string::size_type>(strlen), '\0');
  iss.read(str.data(), zit::numeric_cast<std::streamsize>(strlen));
  if (iss.eof()) {
    throw std::invalid_argument("String not of expected length");
  }
  return Element::build(str);
}

[[noreturn]] inline void throw_invalid_string(std::istringstream& iss) {
  std::stringstream console_safe;
  for (const char c : iss.str().substr(0, MAX_INVALID_STRING_LENGTH)) {
    console_safe << (c > ASCII_LAST_CTRL_CHAR ? c : '?');
  }
  const auto pos = iss.tellg();
  iss.seekg(0, std::ios::end);
  if (iss.tellg() > MAX_INVALID_STRING_LENGTH) {
    console_safe << "...";
  }

  const auto pos_str =
      pos == std::istringstream::pos_type(-1) ? "?" : std::to_string(pos);
  throw std::invalid_argument("Invalid bencode string: '" + console_safe.str() +
                              "' at position " + pos_str + "\n");
}

[[nodiscard]] ElmPtr decode_internal(std::istringstream& iss, unsigned int);

[[nodiscard]] inline ElmPtr decodeList(std::istringstream& iss,
                                       const unsigned int depth) {
  iss.ignore();
  auto v = BeList();
  if (iss.peek() != 'e') {
    while (true) {
      v.push_back(decode_internal(iss, depth));
      if (iss.eof()) {
        throw std::invalid_argument("Unexpected eof: " + iss.str());
      }
      if (iss.peek() == 'e') {
        break;
      }
    }
  }
  iss.ignore();
  return Element::build(v);
}

[[nodiscard]] inline ElmPtr decodeDict(std::istringstream& iss,
                                       const unsigned int depth) {
  iss.ignore();
  auto m = BeDict();
  if (iss.peek() != 'e') {
    while (true) {
      const auto key = decodeString(iss);
      const auto val = decode_internal(iss, depth);
      m[key->to<TypedElement<std::string>>()->val()] = val;
      if (iss.eof()) {
        throw std::invalid_argument("Unexpected eof: " + iss.str());
      }
      if (iss.peek() == 'e') {
        break;
      }
    }
  }
  iss.ignore();
  return Element::build(m);
}

/**
 * @note Do not call this directly, use @ref decode() instead.
 */
[[nodiscard]] inline ElmPtr decode_internal(std::istringstream& iss,
                                            unsigned int depth) {
  if (depth++ > RECURSION_LIMIT) {
    throw std::invalid_argument("Recursion limit reached");
  }
  if (iss.peek() == 'i') {
    iss.ignore();
    return decodeInt(iss);
  }
  if (iss.peek() >= '0' && iss.peek() <= '9') {
    return decodeString(iss);
  }
  if (iss.peek() == 'l') {
    return decodeList(iss, depth);
  }
  if (iss.peek() == 'd') {
    return decodeDict(iss, depth);
  }
  throw_invalid_string(iss);
}

/**
 * Decode bencoded string.
 *
 * @param str Bencoded string to decode.
 *
 * @return ElmPtr to the root element. @ref Element::to() can then be used to
 *   convert this to the underlying value.
 *
 * @throws std::invalid_argument if the input string is not valid bencode
 *   format.
 */
[[nodiscard]] inline ElmPtr decode(const std::string& str) {
  std::istringstream iss(str);
  auto elm = decode_internal(iss, 0);
  if (!elm || !iss.ignore().eof()) {
    throw_invalid_string(iss);
  }
  return elm;
}

/**
 * Pretty print decoded data.
 */
inline std::ostream& operator<<(std::ostream& os, const ElmPtr& elmPtr) {
  return elmPtr->print(os);
}

/** For fmt */
inline std::string format_as(const ElmPtr& elmPtr) {
  std::stringstream ss;
  ss << elmPtr;
  return ss.str();
}

}  // namespace bencode
