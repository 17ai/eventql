/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <fnord/stdtypes.h>
#include <fnord/UnixTime.h>
#include <fnord/option.h>
#include <fnord/SHA1.h>

using namespace fnord;

namespace tsdb {

struct TSDBTableRef {
  static TSDBTableRef parse(const String& table_ref);

  String table_key;
  Option<String> host;
  Option<SHA1Hash> partition_key;
  Option<UnixTime> timerange_begin;
  Option<UnixTime> timerange_limit;
};

} // namespace csql
