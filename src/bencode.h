// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace bencode {

// Forward declarations
template <typename T>
class TypedElement;
class Element;

// Typedefs
using ElmPtr = std::shared_ptr<Element>;
using BeDict = std::map<std::string, ElmPtr>;
using BeList = std::vector<ElmPtr>;

// For stream indentation level
static const auto indent_index = std::ios_base::xalloc();

/**
 * These two templates are used to convert arrays to pointer specifically to
 * allow a char[] to use the std::string specialization.
 */
template <class T>
struct array_to_pointer_decay {
  using type = T;
};

/**
 * These two templates are used to convert arrays to pointer specifically to
 * allow a char[] to use the std::string specialization.
 */
template <class T, std::size_t N>
struct array_to_pointer_decay<T[N]> {
  using type = const T*;
};

// Function templates
template <typename T>
std::string encode_internal(const T& in) {
  std::stringstream ss;
  ss << "i" << in << "e";
  return ss.str();
}

template <typename T>
std::string encode(const T& in) {
  using src = typename array_to_pointer_decay<T>::type;
  return encode_internal<src>(in);
}

// Exceptions

/**
 * Thrown when conversion between types happens.
 */
class bencode_conversion_error : public std::runtime_error {
 public:
  explicit bencode_conversion_error(const std::string& what_arg)
      : std::runtime_error(what_arg) {}
};

// Classes
class Element : public std::enable_shared_from_this<Element> {
 public:
  virtual ~Element() = default;
  virtual std::string encode() const = 0;

  template <typename T>
  auto to() const {
    static_assert(std::is_base_of<Element, T>::value,
                  "Can only return sublasses of Element");
    auto ptr = std::dynamic_pointer_cast<const T>(shared_from_this());
    if (!ptr) {
      throw bencode_conversion_error("Could not convert to type");
    }
    return ptr;
  }

  virtual std::ostream& print(std::ostream& os) = 0;

  /**
   * First try used T&& val, but it couse storage of int references when we want
   * such values copied. See https://stackoverflow.com/q/17316386/11722 and
   * https://isocpp.org/blog/2012/11/universal-references-in-c11-scott-meyers
   * for details.
   */
  template <typename T>
  static auto build(T val) {
    return std::make_shared<TypedElement<T>>(std::move(val));
  }

  static auto build(const char* val) { return build(std::string(val)); }
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
  std::string encode() const override { return bencode::encode(m_data); }

  auto val() const { return m_data; }

  /**
   * Pretty print to console.
   */
  std::ostream& print(std::ostream& os) override {
    if constexpr (std::is_same<T, std::map<std::string, ElmPtr>>()) {
      auto indent = [&os]() {
        for (int i = 0; i < os.iword(indent_index); ++i) {
          os << " ";
        }
      };

      os << "{\n";
      os.iword(indent_index) += 2;
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
      os.iword(indent_index) -= 2;
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
      for (char c : m_data.substr(0, 70)) {
        console_safe << (c > 31 ? c : '?');
      }
      os << console_safe.str();
      if (m_data.length() > 70) {
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
  const T m_data;
};

// Template specializations
template <>
inline std::string encode_internal(const std::string& str) {
  std::stringstream ss;
  ss << str.length() << ":" << str;
  return ss.str();
}

template <>
inline std::string encode_internal(const BeList& elist) {
  std::stringstream ss;
  ss << "l";
  for (const auto& elm : elist) {
    ss << elm->encode();
  }
  ss << "e";
  return ss.str();
}

template <>
inline std::string encode_internal(const char* const& in) {
  return encode_internal(std::string(in));
}

template <>
inline std::string encode_internal(const BeDict& emap) {
  std::stringstream ss;
  ss << "d";
  for (const auto& elm : emap) {
    ss << encode(elm.first) << elm.second->encode();
  }
  ss << "e";
  return ss.str();
}

inline ElmPtr decodeInt(std::istringstream& iss) {
  int64_t i64;
  iss >> i64;
  if (iss.fail()) {
    throw std::invalid_argument("Could not convert to integer");
  }
  if (iss.get() != 'e') {
    throw std::invalid_argument("No integer end marker");
  }
  return Element::build(i64);
}

inline ElmPtr decodeString(std::istringstream& iss) {
  uint64_t strlen;
  iss >> strlen;
  if (iss.fail()) {
    throw std::invalid_argument("Could not convert string length to integer");
  }
  if (iss.get() != ':') {
    throw std::invalid_argument("No string length end marker");
  }
  std::string str(strlen, '\0');
  iss.read(&str[0], strlen);
  if (iss.eof()) {
    throw std::invalid_argument("String not of expected length");
  }
  return Element::build(str);
}

[[noreturn]] inline void throw_invalid_string(std::istringstream& iss) {
  std::stringstream console_safe;
  for (char c : iss.str().substr(0, 128)) {
    console_safe << (c > 31 ? c : '?');
  }
  auto pos = static_cast<size_t>(iss.tellg());
  iss.seekg(0, std::ios::end);
  if (iss.tellg() > 128) {
    console_safe << "...";
  }

  throw std::invalid_argument("Invalid bencode string: '" + console_safe.str() +
                              "' at position " + std::to_string(pos) + "\n");
}

ElmPtr decode_internal(std::istringstream& iss);

inline ElmPtr decodeList(std::istringstream& iss) {
  iss.ignore();
  auto v = BeList();
  if (iss.peek() != 'e') {
    while (true) {
      v.push_back(decode_internal(iss));
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

inline ElmPtr decodeDict(std::istringstream& iss) {
  iss.ignore();
  auto m = BeDict();
  if (iss.peek() != 'e') {
    while (true) {
      auto key = decodeString(iss);
      auto val = decode_internal(iss);
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
inline ElmPtr decode_internal(std::istringstream& iss) {
  if (iss.peek() == 'i') {
    iss.ignore();
    return decodeInt(iss);
  } else if (iss.peek() >= '0' && iss.peek() <= '9') {
    return decodeString(iss);
  } else if (iss.peek() == 'l') {
    return decodeList(iss);
  } else if (iss.peek() == 'd') {
    return decodeDict(iss);
  }
  throw_invalid_string(iss);
}

/**
 * Decode bencoded string.
 *
 * @param str Bencoded string to decode.
 *
 * @return ElmPtr to the root element. @ref Element::to() can then be used to
 *   conver this to the underlying value.
 *
 * @throws std::invalid_argument if the input string is not valid bencode
 *   format.
 */
inline ElmPtr decode(const std::string& str) {
  std::istringstream iss(str);
  auto elm = decode_internal(iss);
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

}  // namespace bencode
