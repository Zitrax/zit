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

// Function templates
template <typename T>
std::string encode(const T& in) {
  std::stringstream ss;
  ss << "i" << in << "e";
  return ss.str();
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

  static auto build(const char* val) { return build(std::string(val)); }
};

template <typename T>
class TypedElement : public Element {
 public:
  TypedElement(const T& data) : m_data(data) {}
  std::string encode() const override { return bencode::encode(m_data); }

 private:
  T m_data;
};

// Typedefs
using ElmPtr = std::unique_ptr<Element>;

// Template specializations
template <>
inline std::string encode(const std::string& str) {
  std::stringstream ss;
  ss << str.length() << ":" << str;
  return ss.str();
}

template <>
inline std::string encode(const std::vector<ElmPtr>& elist) {
  std::stringstream ss;
  ss << "I";
  for (const auto& elm : elist) {
    ss << elm->encode();
  }
  ss << "e";
  return ss.str();
}

/**
 * A map of strings to other types
 */
using BencodeMap = std::map<std::string, ElmPtr>;

template <>
inline std::string encode(const BencodeMap& emap) {
  std::stringstream ss;
  ss << "d";
  for (const auto& elm : emap) {
    ss << encode(elm.first) << elm.second->encode();
  }
  ss << "e";
  return ss.str();
}

}  // namespace bencode
