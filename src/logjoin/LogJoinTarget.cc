/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include "fnord-msg/MessageBuilder.h"
#include "fnord-msg/MessageObject.h"
#include "fnord-msg/MessageEncoder.h"
#include "fnord-msg/MessageDecoder.h"
#include "fnord-msg/MessagePrinter.h"
#include <fnord-fts/fts.h>
#include <fnord-fts/fts_common.h>
#include "logjoin/LogJoinTarget.h"
#include "common.h"

using namespace fnord;

namespace cm {

LogJoinTarget::LogJoinTarget(
    const msg::MessageSchema& joined_sessions_schema,
    fts::Analyzer* analyzer,
    RefPtr<DocIndex> index,
    bool dry_run) :
    joined_sessions_schema_(joined_sessions_schema),
    analyzer_(analyzer),
    index_(index),
    dry_run_(dry_run),
    num_sessions(0),
    cconv_(currencyConversionTable()) {}

void LogJoinTarget::onSession(
    mdb::MDBTransaction* txn,
    TrackedSession& session) {
  session.joinEvents(cconv_);

  const auto& schema = joined_sessions_schema_;
  msg::MessageObject obj;

  for (const auto& ci : session.cart_items) {
    auto& ci_obj = obj.addChild(schema.id("cart_items"));

    ci_obj.addChild(
        schema.id("cart_items.time"),
        (uint32_t) (ci.time.unixMicros() / kMicrosPerSecond));

    ci_obj.addChild(
        schema.id("cart_items.item_id"),
        ci.item.docID().docid);

    ci_obj.addChild(
        schema.id("cart_items.quantity"),
        ci.quantity);

    ci_obj.addChild(
        schema.id("cart_items.price_cents"),
        ci.price_cents);

    ci_obj.addChild(
        schema.id("cart_items.currency"),
        (uint32_t) currencyFromString(ci.currency));

    ci_obj.addChild(
        schema.id("cart_items.checkout_step"),
        ci.checkout_step);

    // FIXPAUL use getFields...
    auto docid = ci.item.docID();
    auto shopid = index_->getField(docid, "shop_id");
    if (shopid.isEmpty()) {
      fnord::logWarning(
          "cm.logjoin",
          "item not found in featureindex: $0",
          docid.docid);
    } else {
      ci_obj.addChild(
          schema.id("cart_items.shop_id"),
          (uint32_t) std::stoull(shopid.get()));
    }

    auto category1 = index_->getField(docid, "category1");
    if (!category1.isEmpty()) {
      ci_obj.addChild(
          schema.id("cart_items.category1"),
          (uint32_t) std::stoull(category1.get()));
    }

    auto category2 = index_->getField(docid, "category2");
    if (!category2.isEmpty()) {
      ci_obj.addChild(
          schema.id("cart_items.category2"),
          (uint32_t) std::stoull(category2.get()));
    }

    auto category3 = index_->getField(docid, "category3");
    if (!category3.isEmpty()) {
      ci_obj.addChild(
          schema.id("cart_items.category3"),
          (uint32_t) std::stoull(category3.get()));
    }
  }

  uint32_t sess_abgrp = 0;
  for (const auto& q : session.queries) {
    auto& qry_obj = obj.addChild(schema.id("queries"));

    /* queries.time */
    qry_obj.addChild(
        schema.id("search_queries.time"),
        (uint32_t) (q.time.unixMicros() / kMicrosPerSecond));

    /* queries.language */
    auto lang = cm::extractLanguage(q.attrs);
    qry_obj.addChild(schema.id("search_queries.language"), (uint32_t) lang);

    /* queries.query_string */
    auto qstr = cm::extractQueryString(q.attrs);
    if (!qstr.isEmpty()) {
      auto qstr_norm = analyzer_->normalize(lang, qstr.get());
      qry_obj.addChild(schema.id("search_queries.query_string"), qstr.get());
      qry_obj.addChild(schema.id("search_queries.query_string_normalized"), qstr_norm);
    }

    /* queries.shopid */
    auto slrid = cm::extractAttr(q.attrs, "slrid");
    if (!slrid.isEmpty()) {
      uint32_t sid = std::stoul(slrid.get());
      qry_obj.addChild(schema.id("search_queries.shop_id"), sid);
    }

    qry_obj.addChild(schema.id("search_queries.num_result_items"), q.nitems);
    qry_obj.addChild(schema.id("search_queries.num_result_items_clicked"), q.nclicks);
    qry_obj.addChild(schema.id("search_queries.num_ad_impressions"), q.nads);
    qry_obj.addChild(schema.id("search_queries.num_ad_clicks"), q.nadclicks);
    qry_obj.addChild(schema.id("search_queries.num_cart_items"), q.num_cart_items);
    qry_obj.addChild(schema.id("search_queries.cart_value_eurcents"), q.cart_value_eurcents);
    qry_obj.addChild(schema.id("search_queries.num_order_items"), q.num_order_items);
    qry_obj.addChild(schema.id("search_queries.gmv_eurcents"), q.gmv_eurcents);

    /* queries.page */
    auto pg_str = cm::extractAttr(q.attrs, "pg");
    if (!pg_str.isEmpty()) {
      uint32_t pg = std::stoul(pg_str.get());
      qry_obj.addChild(schema.id("search_queries.page"), pg);
    }

    /* queries.ab_test_group */
    auto abgrp = cm::extractABTestGroup(q.attrs);
    if (!abgrp.isEmpty()) {
      sess_abgrp = abgrp.get();
      qry_obj.addChild(schema.id("search_queries.ab_test_group"), abgrp.get());
    }

    /* queries.category1 */
    auto qcat1 = cm::extractAttr(q.attrs, "q_cat1");
    if (!qcat1.isEmpty()) {
      uint32_t c = std::stoul(qcat1.get());
      qry_obj.addChild(schema.id("search_queries.category1"), c);
    }

    /* queries.category1 */
    auto qcat2 = cm::extractAttr(q.attrs, "q_cat2");
    if (!qcat2.isEmpty()) {
      uint32_t c = std::stoul(qcat2.get());
      qry_obj.addChild(schema.id("search_queries.category2"), c);
    }

    /* queries.category1 */
    auto qcat3 = cm::extractAttr(q.attrs, "q_cat3");
    if (!qcat3.isEmpty()) {
      uint32_t c = std::stoul(qcat3.get());
      qry_obj.addChild(schema.id("search_queries.category3"), c);
    }

    /* queries.device_type */
    qry_obj.addChild(
        schema.id("search_queries.device_type"),
        (uint32_t) extractDeviceType(q.attrs));

    /* queries.page_type */
    qry_obj.addChild(
        schema.id("search_queries.page_type"),
        (uint32_t) extractPageType(q.attrs));

    for (const auto& item : q.items) {
      auto& item_obj = qry_obj.addChild(
          schema.id("search_queries.result_items"));

      item_obj.addChild(
          schema.id("search_queries.result_items.position"),
          (uint32_t) item.position);

      item_obj.addChild(
          schema.id("search_queries.result_items.item_id"),
          item.item.docID().docid);

      if (item.clicked) {
        item_obj.addChild(
            schema.id("search_queries.result_items.clicked"),
            msg::TRUE);
      } else {
        item_obj.addChild(
            schema.id("search_queries.result_items.clicked"),
            msg::FALSE);
      }

      auto docid = item.item.docID();

      auto shopid = index_->getField(docid, "shop_id");
      if (shopid.isEmpty()) {
        fnord::logWarning(
            "cm.logjoin",
            "item not found in featureindex: $0",
            docid.docid);
      } else {
        item_obj.addChild(
            schema.id("search_queries.result_items.shop_id"),
            (uint32_t) std::stoull(shopid.get()));
      }

      auto category1 = index_->getField(docid, "category1");
      if (!category1.isEmpty()) {
        item_obj.addChild(
            schema.id("search_queries.result_items.category1"),
            (uint32_t) std::stoull(category1.get()));
      }

      auto category2 = index_->getField(docid, "category2");
      if (!category2.isEmpty()) {
        item_obj.addChild(
            schema.id("search_queries.result_items.category2"),
            (uint32_t) std::stoull(category2.get()));
      }

      auto category3 = index_->getField(docid, "category3");
      if (!category3.isEmpty()) {
        item_obj.addChild(
            schema.id("search_queries.result_items.category3"),
            (uint32_t) std::stoull(category3.get()));
      }
    }
  }

  for (const auto& iv : session.item_visits) {
    auto& iv_obj = obj.addChild(schema.id("item_visits"));

    iv_obj.addChild(
        schema.id("item_visits.time"),
        (uint32_t) (iv.time.unixMicros() / kMicrosPerSecond));

    iv_obj.addChild(
        schema.id("item_visits.item_id"),
        iv.item.docID().docid);

    auto docid = iv.item.docID();
    auto shopid = index_->getField(docid, "shop_id");
    if (shopid.isEmpty()) {
      fnord::logWarning(
          "cm.logjoin",
          "item not found in featureindex: $0",
          docid.docid);
    } else {
      iv_obj.addChild(
          schema.id("item_visits.shop_id"),
          (uint32_t) std::stoull(shopid.get()));
    }

    auto category1 = index_->getField(docid, "category1");
    if (!category1.isEmpty()) {
      iv_obj.addChild(
          schema.id("item_visits.category1"),
          (uint32_t) std::stoull(category1.get()));
    }

    auto category2 = index_->getField(docid, "category2");
    if (!category2.isEmpty()) {
      iv_obj.addChild(
          schema.id("item_visits.category2"),
          (uint32_t) std::stoull(category2.get()));
    }

    auto category3 = index_->getField(docid, "category3");
    if (!category3.isEmpty()) {
      iv_obj.addChild(
          schema.id("item_visits.category3"),
          (uint32_t) std::stoull(category3.get()));
    }
  }

  if (sess_abgrp > 0) {
    obj.addChild(schema.id("ab_test_group"), sess_abgrp);
  }

  obj.addChild(schema.id("num_cart_items"), session.num_cart_items);
  obj.addChild(schema.id("cart_value_eurcents"), session.cart_value_eurcents);
  obj.addChild(schema.id("num_order_items"), session.num_order_items);
  obj.addChild(schema.id("gmv_eurcents"), session.gmv_eurcents);

  auto first_seen = session.firstSeenTime();
  if (!first_seen.isEmpty()) {
    obj.addChild(
        schema.id("first_seen_time"),
        first_seen.get().unixMicros() / kMicrosPerSecond);
  }

  auto last_seen = session.lastSeenTime();
  if (!last_seen.isEmpty()) {
    obj.addChild(
        schema.id("last_seen_time"),
        last_seen.get().unixMicros() / kMicrosPerSecond);
  }

  if (dry_run_) {
    fnord::logInfo(
        "cm.logjoin",
        "[DRYRUN] not uploading session: $0",
        msg::MessagePrinter::print(obj, joined_sessions_schema_));
  } else {
    Buffer msg_buf;
    msg::MessageEncoder::encode(obj, joined_sessions_schema_, &msg_buf);
    auto key = StringUtil::format("__uploadq-sessions-$0",  rnd_.hex128());
    txn->update(key, msg_buf);
  }

  ++num_sessions;
}

} // namespace cm

