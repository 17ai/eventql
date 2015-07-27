/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#pragma once
#include "stx/protobuf/MessageSchema.h"
#include "common/CustomerConfig.h"
#include "analytics/TableDefinition.h"

using namespace stx;

namespace cm {

struct SessionSchema {

  static RefPtr<msg::MessageSchema> forCustomer(const CustomerConfig& cfg);

  static Vector<TableDefinition> tableDefinitionsForCustomer(
      const CustomerConfig& cfg);

};

} // namespace cm
