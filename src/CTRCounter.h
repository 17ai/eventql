/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#ifndef _CM_CTRCOUNTER_H
#define _CM_CTRCOUNTER_H
#include "fnord/stdtypes.h"
#include "fnord/option.h"
#include "fnord/json/json.h"
#include "sstable/sstablereader.h"
#include "sstable/sstablewriter.h"
#include "sstable/SSTableColumnSchema.h"
#include "sstable/SSTableColumnReader.h"
#include "sstable/SSTableColumnWriter.h"

using namespace fnord;

namespace cm {

struct CTRCounterData {
  static CTRCounterData load(const String& buf);
  static sstable::SSTableColumnSchema sstableSchema();

  CTRCounterData();
  void merge(const CTRCounterData& other);
  void encode(util::BinaryMessageWriter* writer) const;
  void decode(util::BinaryMessageReader* reader);
  void toJSON(json::JSONOutputStream* json) const;

  uint64_t num_views;
  uint64_t num_clicked;
  uint64_t num_clicks;
  uint64_t gmv_eurcent;
  uint64_t cart_value_eurcent;
};

typedef Pair<String, CTRCounterData> CTRCounter;

}
#endif
