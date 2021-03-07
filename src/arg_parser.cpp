// -*- mode:c++; c-basic-offset : 2; -*-
#include "arg_parser.h"

#include "spdlog/fmt/fmt.h"
#include "types.h"

using namespace std;

namespace zit {

template <typename T>
void ArgParser::add_option(const string& option,
                           const optional<T>& def,
                           const string& help,
                           T& dst,
                           bool required) {
  if (std::find_if(m_options.begin(), m_options.end(), [&](const auto& a) {
        return a->m_option == option;
      }) != m_options.end()) {
    throw runtime_error(fmt::format("Duplicate option '{}' added", option));
  }

  if (std::find_if(m_options.begin(), m_options.end(), [&](const auto& a) {
        return a->dst() == &dst;
      }) != m_options.end()) {
    throw runtime_error(
        fmt::format("Duplicate value reference for '{}' added", option));
  }

  if (def) {
    dst = def.value();
  }
  auto argType = []() {
    if constexpr (std::is_same_v<T, bool>) {
      return Type::BOOL;
    } else if (std::is_same_v<T, float>) {
      return Type::FLOAT;
    } else if (std::is_same_v<T, int>) {
      return Type::INT;
    } else if (std::is_same_v<T, unsigned>) {
      return Type::UINT;
    } else if (std::is_same_v<T, string>) {
      return Type::STRING;
    }
  };

  m_options.emplace_back(
      std::make_unique<Arg<T>>(option, help, argType(), dst, required));
}

void ArgParser::add_help_option(const std::string& option,
                                const std::string& help,
                                bool& dst) {
  add_option(option, {false}, help, dst, false);
  m_options.back()->m_help_arg = true;
}

template void ArgParser::add_option<bool>(const string&,
                                          const optional<bool>&,
                                          const string&,
                                          bool&,
                                          bool);
template void ArgParser::add_option<int>(const string&,
                                         const optional<int>&,
                                         const string&,
                                         int&,
                                         bool);
template void ArgParser::add_option<unsigned>(const string&,
                                              const optional<unsigned>&,
                                              const string&,
                                              unsigned&,
                                              bool);
template void ArgParser::add_option<float>(const string&,
                                           const optional<float>&,
                                           const string&,
                                           float&,
                                           bool);
template void ArgParser::add_option<string>(const string&,
                                            const optional<string>&,
                                            const string&,
                                            string&,
                                            bool);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
void ArgParser::parse(int argc, const char* argv[]) {
  if (m_parsed) {
    throw runtime_error("Options already parsed");
  }
  m_parsed = true;
  for (int i = 1; i < argc; i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::string_view name = argv[i];
    auto m = std::find_if(m_options.begin(), m_options.end(),
                          [&](const auto& a) { return a->m_option == name; });
    if (m == m_options.end()) {
      throw runtime_error(fmt::format("Unknown argument: {}", name));
    }
    auto& arg = *m;

    if (arg->m_type == Type::BOOL) {
      as<bool>(arg).m_dst = true;
    } else {
      // Read next arg
      if (i == argc - 1) {
        throw runtime_error(fmt::format("Missing value for {}", name));
      }
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      const char* val = argv[i++ + 1];

      switch (arg->m_type) {
        case Type::FLOAT: {
          float fval = std::strtof(val, nullptr);
          if (errno == ERANGE) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          as<float>(arg).m_dst = fval;
        } break;
        case Type::INT:
          try {
            as<int>(arg).m_dst =
                numeric_cast<int>(std::strtol(val, nullptr, 10));
          } catch (const std::out_of_range&) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          break;
        case Type::UINT:
          try {
            as<unsigned>(arg).m_dst =
                numeric_cast<unsigned>(std::strtol(val, nullptr, 10));
          } catch (const std::out_of_range&) {
            throw std::out_of_range(fmt::format(
                "Value for argument '{}' is out of range for type", name));
          }
          break;
        case Type::STRING:
          as<string>(arg).m_dst = val;
          break;
        case Type::BOOL:
          throw runtime_error("Invalid option type");
      }
    }
    arg->m_provided = true;
  }

  // Check if all options got values
  auto nop = std::find_if(
      m_options.begin(), m_options.end(),
      [](const auto& o) { return !o->m_provided && o->m_required; });
  if (nop != m_options.end()) {
    if (!std::any_of(m_options.begin(), m_options.end(), [](const auto& o) {
          return o->m_help_arg && o->m_provided;
        })) {
      throw runtime_error(
          fmt::format("Required option '{}' not provided", (*nop)->m_option));
    }
  }
}

std::string ArgParser::usage() {
  stringstream ss;
  ss << "Usage:\n\n" << m_desc << "\n\n";

  auto it = std::max_element(m_options.begin(), m_options.end(),
                             [](const auto& a, const auto& b) {
                               return a->m_option.size() < b->m_option.size();
                             });
  if (it == m_options.end()) {
    return ss.str();
  }
  const auto width = (*it)->m_option.size();

  for (const auto& option : m_options) {
    ss << fmt::format("  {:{}}    {} {}\n", option->m_option, width,
                      option->m_help, option->m_required ? "(required)"s : ""s);
  }
  return ss.str();
}

}  // namespace zit
