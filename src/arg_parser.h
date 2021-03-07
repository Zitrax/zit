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

  /**
   * Add an option to be parsed by parse().
   */
  template <typename T>
  void add_option(const std::string& option,
                  const std::optional<T>& def,
                  const std::string& help,
                  T& dst,
                  bool required = false);

  /**
   * Add specific help option. This can be passed alone without required flags.
   */
  void add_help_option(const std::string& option,
                       const std::string& help,
                       bool& dst);

  /**
   * Parse arguments for options provided by add_option.
   */
  void parse(int argc, const char* argv[]);

  /**
   * Get help info string for all available options.
   */
  std::string usage();

 private:
  enum class Type { INT, UINT, FLOAT, STRING, BOOL };

  struct BaseArg {
    BaseArg(const std::string& option,
            const std::string& help,
            Type type,
            bool required)
        : m_option(option), m_help(help), m_type(type), m_required(required) {}
    virtual ~BaseArg() = default;
    virtual void* dst() = 0;
    std::string m_option;
    std::string m_help;
    Type m_type;
    bool m_provided = false;
    bool m_required = false;
    bool m_help_arg = false;
  };

  template <typename T>
  struct Arg : public BaseArg {
   public:
    Arg(const std::string& option,
        const std::string& help,
        Type type,
        T& dst,
        bool required)
        : BaseArg(option, help, type, required), m_dst(dst) {}
    virtual void* dst() override { return &m_dst; }
    T& m_dst{};
  };

  template <typename T>
  auto& as(std::unique_ptr<ArgParser::BaseArg>& arg) {
    return dynamic_cast<Arg<T>&>(*arg);
  }

  std::string m_desc;
  std::vector<std::unique_ptr<BaseArg>> m_options{};
  bool m_parsed = false;
};
}  // namespace zit
