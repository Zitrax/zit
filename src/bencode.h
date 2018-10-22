// -*- mode:c++; c-basic-offset : 2; - * -
#pragma once

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

/**
 * These two templates are used to convert arrays to pointer specifically to
 * allow a char[] to use the std::string specialization.
 */
template <class T>
struct array_to_pointer_decay {
  typedef T type;
};

/**
 * These two templates are used to convert arrays to pointer specifically to
 * allow a char[] to use the std::string specialization.
 */
template <class T, std::size_t N>
struct array_to_pointer_decay<T[N]> {
  typedef const T* type;
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
  typedef typename array_to_pointer_decay<T>::type src;
  return encode_internal<src>(in);
}

// Classes
class Element : public std::enable_shared_from_this<Element> {
 public:
  virtual ~Element() = default;
  virtual std::string encode() const = 0;

  template <typename T>
  auto to() {
    static_assert(std::is_base_of<Element, T>::value,
                  "Can only return sublasses of Element");
    auto ptr = std::dynamic_pointer_cast<T>(shared_from_this());
    if (!ptr) {
      throw std::runtime_error("Could not convert to type");
    }
    return ptr;
  }

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
  TypedElement(std::add_rvalue_reference_t<T> data)
      : m_data(std::forward<T>(data)) {}
  std::string encode() const override { return bencode::encode(m_data); }

  operator T() const { return m_data; }
  auto val() const { return m_data; }

 private:
  const T m_data;
};

// Typedefs
using ElmPtr = std::shared_ptr<Element>;

// Template specializations
template <>
inline std::string encode_internal(const std::string& str) {
  std::stringstream ss;
  ss << str.length() << ":" << str;
  return ss.str();
}

template <>
inline std::string encode_internal(const std::vector<ElmPtr>& elist) {
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

/**
 * A map of strings to other types
 */
using BencodeMap = std::map<std::string, ElmPtr>;

template <>
inline std::string encode_internal(const BencodeMap& emap) {
  std::stringstream ss;
  ss << "d";
  for (const auto& elm : emap) {
    ss << encode(elm.first) << elm.second->encode();
  }
  ss << "e";
  return ss.str();
}

ElmPtr decode(const std::string& str) {
  if (str.empty()) {
    throw std::invalid_argument("Empty string");
  }

  if (str[0] == 'i') {
    std::istringstream iss(str.substr(1));
    int64_t i64;
    iss >> i64;
    if (iss.fail()) {
      throw std::invalid_argument("Could not convert to integer");
    }
    if (str.at(static_cast<int>(iss.tellg()) + 1) != 'e') {
      throw std::invalid_argument("No integer end marker");
    }
    return Element::build(i64);
  } else if (str[0] >= '0' && str[0] <= '9') {
    std::istringstream iss(str);
    uint64_t strlen;
    iss >> strlen;
    if (iss.fail()) {
      throw std::invalid_argument("Could not convert string length to integer");
    }
    char c;
    try {
      c = str.at(iss.tellg());
    } catch (const std::out_of_range& ex) { /* throw below instead */
    }
    if (c != ':') {
      throw std::invalid_argument("No string length end marker");
    }
    auto ret = str.substr(static_cast<int>(iss.tellg()) + 1, strlen);
    if (ret.length() != strlen) {
      throw std::invalid_argument("String not of expected length");
    }
    return Element::build(ret);
  }

  throw std::invalid_argument("Invalid bencode string: " + str);
}

}  // namespace bencode
