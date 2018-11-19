// -*- mode:c++; c-basic-offset : 2; - * -
#pragma once

#include <string>

namespace zit {

class Net {
 public:
  Net() = default;

  // TODO: url param and split/parse internally
  std::tuple<std::string, std::string> http_get(const std::string& server,
                                                const std::string& path = "");
};

}  // namespace zit
