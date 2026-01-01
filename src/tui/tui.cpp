#include "controller.hpp"

int main(int /*argc*/, const char* /*argv*/[]) noexcept {
  zit::tui::TuiController controller;
  return controller.Run();
}
