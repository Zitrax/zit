// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <fmt/core.h>
#include <algorithm>
#include <any>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace zit {

/**
 * Simple argument parser
 */
class ArgParser {
 private:
  enum class Type { INT, UINT, FLOAT, STRING, BOOL };

  struct BaseArg {
    explicit BaseArg(std::string option, Type type)
        : m_option(std::move(option)), m_type(type) {}

    virtual ~BaseArg() = default;
    virtual void* dst() = 0;

    [[nodiscard]] auto get_option() const { return m_option; }
    [[nodiscard]] const auto& get_aliases() const { return m_aliases; }
    [[nodiscard]] auto get_help() const { return m_help; }
    [[nodiscard]] auto get_type() const { return m_type; }
    [[nodiscard]] auto is_provided() const { return m_provided; }
    [[nodiscard]] auto is_required() const { return m_required; }
    [[nodiscard]] auto is_help_arg() const { return m_help_arg; }

    void set_help_arg(bool help_arg) { m_help_arg = help_arg; }
    void set_provided(bool provided) { m_provided = provided; }

   protected:
    std::string m_option;
    std::string m_help{};
    std::set<std::string> m_aliases{};
    Type m_type;
    bool m_provided = false;
    bool m_required = false;
    bool m_help_arg = false;
  };

  template <typename T>
  static constexpr auto typeEnum() {
    if constexpr (std::is_same_v<T, bool>) {
      return Type::BOOL;
    } else if (std::is_same_v<T, float>) {
      return Type::FLOAT;
    } else if (std::is_same_v<T, int>) {
      return Type::INT;
    } else if (std::is_same_v<T, unsigned>) {
      return Type::UINT;
    } else if (std::is_same_v<T, std::string>) {
      return Type::STRING;
    }
  }

  using ConstArgIterator =
      std::vector<std::unique_ptr<BaseArg>>::const_iterator;
  using ArgIterator = std::vector<std::unique_ptr<BaseArg>>::iterator;

  template <typename T>
  struct Arg : public BaseArg {
   public:
    explicit Arg(std::string option)
        : BaseArg(std::move(option), ArgParser::typeEnum<T>()) {
      if constexpr (std::is_same_v<T, bool>) {
        // Special for bool - a non-provided arg always default to false
        m_dst = false;
      }
    }

    void* dst() override { return &m_dst; }

    [[nodiscard]] auto dst() const { return m_dst; }
    void set_dst(T t) { m_dst = std::move(t); }

    auto& help(const std::string& help) {
      m_help = help;
      return *this;
    }

    auto& required() {
      m_required = true;
      return *this;
    }

    auto& default_value(T t) {
      m_dst = std::move(t);
      return *this;
    }

    auto& help_arg() {
      m_help_arg = true;
      return *this;
    }

    auto& aliases(const std::set<std::string>& aliases) {
      m_aliases = aliases;
      return *this;
    }

   private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::optional<T> m_dst{};
  };

  template <typename T>
  auto& as(std::unique_ptr<ArgParser::BaseArg>& arg) {
    return dynamic_cast<Arg<T>&>(*arg);
  }

  [[nodiscard]] ConstArgIterator find(const std::string& option) const;
  [[nodiscard]] ArgIterator find(const std::string& option);

  [[nodiscard]] bool has_option(const std::string& option) const;

  std::string m_desc;
  std::vector<std::unique_ptr<BaseArg>> m_options{};
  bool m_parsed = false;

 public:
  explicit ArgParser(std::string desc) : m_desc(std::move(desc)) {}

  template <typename T>
  Arg<T>& add_option(std::string option) {
    if (has_option(option)) {
      throw std::runtime_error(
          fmt::format("Duplicate option '{}' added", option));
    }

    m_options.emplace_back(std::make_unique<Arg<T>>(option));
    return static_cast<Arg<T>&>(*m_options.back());
  }

  template <typename T>
  [[nodiscard]] T get(const std::string& option) const {
    const auto match = find(option);
    if (match == m_options.end()) {
      throw std::runtime_error("No option: " + option);
    }
    const auto* opt = dynamic_cast<const Arg<T>*>(match->get());
    if (!opt) {
      throw std::runtime_error("Invalid type for option: " + option);
    }
    if (opt->is_required() && !opt->is_provided()) {
      throw std::runtime_error("No value for required option: " + option);
    }
    auto dst = opt->dst();
    if (dst.has_value()) {
      return dst.value();
    }
    throw std::runtime_error("No value provided for option: " + option);
  }

  [[nodiscard]] bool is_provided(const std::string& option) const {
    const auto match = find(option);
    if (match == m_options.end()) {
      throw std::runtime_error("No option: " + option);
    }
    return match->get()->is_provided();
  }

  /**
   * Parse arguments for options provided by add_option.
   */
  void parse(const std::vector<std::string>& argv);

  /**
   * Get help info string for all available options.
   */
  std::string usage();
};

}  // namespace zit
