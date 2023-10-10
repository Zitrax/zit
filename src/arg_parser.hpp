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
 public:
  enum class Type { INT, UINT, FLOAT, STRING, BOOL };

 private:
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
    [[nodiscard]] auto is_multi() const { return m_is_multi; }
    [[nodiscard]] auto position() const { return m_position; }

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
    bool m_is_multi = false;
    std::optional<unsigned> m_position{};
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
        m_default = {false};
      }
    }

    void* dst() override { return &m_dst; }

    [[nodiscard]] auto dst() const { return m_dst.empty() ? m_default : m_dst; }
    void set_dst(T t) {
      if (!m_is_multi && !m_dst.empty()) {
        throw std::runtime_error(
            "Multiple values provided for single value option: " + m_option);
      }
      m_dst.emplace_back(t);
    }

    /** Help text for option */
    auto& help(const std::string& help) {
      m_help = help;
      return *this;
    }

    /** Makes the option required */
    auto& required() {
      m_required = true;
      return *this;
    }

    /** Default value for option */
    auto& default_value(T t) {
      m_default = {t};
      return *this;
    }

    /** Default values for multi value option */
    auto& default_value(std::vector<T> t) {
      if (!m_is_multi && t.size() > 1) {
        throw std::runtime_error(
            "Can't default to more than one value for single value option");
      }
      m_default = t;
      return *this;
    }

    auto& positional(int pos) {
      if (m_is_multi) {
        throw std::runtime_error("Positional argument can't be multi");
      }
      m_position = pos;
      return *this;
    }

    /** Mark this option as providing help, for example for "--help". This will
     * override the check on other required arguments */
    auto& help_arg() {
      m_help_arg = true;
      return *this;
    }

    /** Provide aliases that can be used for the same option */
    auto& aliases(const std::set<std::string>& aliases) {
      m_aliases = aliases;
      return *this;
    }

    /** Allow multiple values for the option. Then use get_multi to retrieve all
     * the values. */
    auto& multi() {
      if (m_position) {
        throw std::runtime_error("Positional argument can't be multi");
      }
      m_is_multi = true;
      return *this;
    }

   private:
    std::vector<T> m_dst{};
    std::vector<T> m_default{};
  };

  template <typename T>
  auto& as(std::unique_ptr<ArgParser::BaseArg>& arg) {
    return dynamic_cast<Arg<T>&>(*arg);
  }

  [[nodiscard]] ConstArgIterator find(const std::string& option) const;
  [[nodiscard]] ArgIterator find(const std::string& option);
  [[nodiscard]] ArgIterator find(unsigned position);

  [[nodiscard]] bool has_option(const std::string& option) const;
  void verify_no_duplicate_positionals() const;

  std::string m_desc;
  std::vector<std::unique_ptr<BaseArg>> m_options{};
  bool m_parsed = false;

  template <typename T>
  [[nodiscard]] auto get_internal(const std::string& option) const {
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
    return std::make_tuple(opt->dst(), opt->is_multi());
  }

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
    auto [dst, multi] = get_internal<T>(option);
    if (multi) {
      throw std::runtime_error(
          "get() called on multi value option, use get_multi");
    }
    if (!dst.empty()) {
      return dst.front();
    }
    throw std::runtime_error("No value provided for option: " + option);
  }

  template <typename T>
  [[nodiscard]] std::vector<T> get_multi(const std::string& option) const {
    auto [dst, multi] = get_internal<T>(option);
    if (!multi) {
      throw std::runtime_error(
          "get?multi() called on single value option, use get");
    }
    if (!dst.empty()) {
      return dst;
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
