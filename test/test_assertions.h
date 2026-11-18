#pragma once

// Consume glog's CHECK before Catch2 defines the test assertion.
#include <glog/logging.h>
#undef CHECK
#include <catch2/catch_test_macros.hpp>
