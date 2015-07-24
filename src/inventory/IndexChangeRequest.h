/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#ifndef _CM_INDEXREQUEST_H
#define _CM_INDEXREQUEST_H
#include <mutex>
#include <stdlib.h>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <fnord/UnixTime.h>
#include <fnord/uri.h>
#include <fnord/reflect/reflect.h>
#include <inventory/ItemRef.h>
#include <inventory/IndexChangeRequest.pb.h>

using namespace fnord;

namespace cm {

struct IndexChangeRequestStruct {
  String customer;
  ItemRef item;
  HashMap<String, String> attrs;

  IndexChangeRequest toIndexChangeRequest() const {
    IndexChangeRequest icr;
    icr.set_customer(customer);
    icr.set_docid(item.docID().docid);
    for (const auto& a : attrs) {
      auto icr_a = icr.add_attributes();
      icr_a->set_key(a.first);
      icr_a->set_value(a.second);
    }
    return icr;
  }

  template <typename T>
  static void reflect(T* meta) {
    meta->prop(&IndexChangeRequestStruct::customer, 1, "customer", false);
    meta->prop(&IndexChangeRequestStruct::item, 2, "docid", false);
    meta->prop(&IndexChangeRequestStruct::attrs, 3, "attributes", false);
  };
};

}
#endif
