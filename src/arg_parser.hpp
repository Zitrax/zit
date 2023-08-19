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
  explicit ArgParser(std::string desc) : m_desc(std::move(desc)) {}

  /**
   * Add an option to be parsed by parse().
   * @param options One arg like "--test", or a comma separated list like
   *   "--test,-t" for providing aliases.
   * @param def Optional default value.
   * @param help Descriptive help text.
   * @param dst Reference to variable to be filled with the option value.
   * @param required True if the option is required.
   * @param help_option True if the option is an help argument. See @ref
   *   add_help_option.
   */
  template <typename T>
  void add_option(const std::string& options,
                  const std::optional<T>& def,
                  const std::string& help,
                  T& dst,
                  bool required = false,
                  bool help_option = false);

  /**
   * Add specific help option. This can be passed alone without required flags.
   */
  void add_help_option(const std::string& option,
                       const std::string& help,
                       bool& dst);

  /**
   * Parse arguments for options provided by add_option.
   */
  void parse(const std::vector<std::string>& argv);

  /**
   * Get help info string for all available options.
   */
  std::string usage();

 private:
  enum class Type { INT, UINT, FLOAT, STRING, BOOL };

  struct BaseArg {
    BaseArg(std::string option, std::string help, Type type, bool required)
        : m_option(std::move(option)),
          m_help(std::move(help)),
          m_type(type),
          m_required(required) {}
    virtual ~BaseArg() = default;
    virtual void* dst() = 0;

    [[nodiscard]] auto option() const { return m_option; }
    [[nodiscard]] auto help() const { return m_help; }
    [[nodiscard]] auto type() const { return m_type; }
    [[nodiscard]] auto provided() const { return m_provided; }
    [[nodiscard]] auto required() const { return m_required; }
    [[nodiscard]] auto help_arg() const { return m_help_arg; }

    void set_help_arg(bool help_arg) { m_help_arg = help_arg; }
    void set_provided(bool provided) { m_provided = provided; }

   private:
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
    void* dst() override { return &m_dst; }

    [[nodiscard]] auto dst() const { return m_dst; }
    void set_dst(T t) { m_dst = std::move(t); }

   private:
   // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
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
