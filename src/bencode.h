#pragma once

#include <list>
#include <memory>
#include <sstream>

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
inline std::string encode<std::string>(const std::string& str) {
  std::stringstream ss;
  ss << str.length() << ":" << str;
  return ss.str();
}

template <>
inline std::string encode(const std::list<ElmPtr>& elist) {
  std::stringstream ss;
  ss << "I";
  for (const auto& elm : elist) {
    ss << elm->encode();
  }
  ss << "e";
  return ss.str();
}

}  // namespace bencode
