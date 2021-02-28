// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zit {

/**
 * Simple argument parser
 */
class ArgParser {
public:
  explicit ArgParser(std::string desc) : m_desc(desc) {}

  template <typename T>
  void add_option(const std::string &option, const std::optional<T> &def,
                  const std::string &help, T &dst, bool required = false);

  void parse(int argc, const char *argv[]);

private:
  enum class Type { INT, UINT, FLOAT, STRING, BOOL };

  struct BaseArg {
    BaseArg(const std::string &option, const std::string &help, Type type,
            bool required)
        : option(option), help(help), type(type), required(required) {}
    virtual ~BaseArg() = default;
    std::string option;
    std::string help;
    Type type;
    bool provided = false;
    bool required = false;
  };

  template <typename T> struct Arg : public BaseArg {
  public:
    Arg(const std::string &option, const std::string &help, Type type, T &dst,
        bool required)
        : BaseArg(option, help, type, required), dst(dst) {}
    T &dst{};
  };

  template <typename T> auto &as(std::unique_ptr<ArgParser::BaseArg> &arg) {
    return dynamic_cast<Arg<T> &>(*arg);
  }

  std::string m_desc;
  std::vector<std::unique_ptr<BaseArg>> m_options;
};
} // namespace zit