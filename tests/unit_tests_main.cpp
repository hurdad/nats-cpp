#include "unit_tests.hpp"

int main() {
  unit_tests::run_core_unit_tests();
  unit_tests::run_api_surface_unit_tests();
  unit_tests::run_integration_tests();
  return 0;
}
