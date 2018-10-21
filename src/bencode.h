// -*- mode:c++; c-basic-offset : 2; - * -
#pragma once

#include <map>
#include <memory>
#include <sstream>
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
class Element {
 public:
  virtual ~Element() = default;
  virtual std::string encode() const = 0;

  template <typename T>
  static auto build(const T& val) {
    return std::make_unique<TypedElement<T>>(val);
  }

  template <typename T>
  static auto build(T&& val) {
    return std::make_unique<TypedElement<T>>(std::forward<T>(val));
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

 private:
  const T m_data;
};

// Typedefs
using ElmPtr = std::unique_ptr<Element>;

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

}  // namespace bencode
