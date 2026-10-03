#pragma once

#include <sqlite3.h>

#include "minion/conf.hpp"

namespace minion::loop {

struct Options {
  bool once = false;
};

[[nodiscard]] int Serve(sqlite3 *db, const conf::Settings &settings,
                        const Options &options);

}
