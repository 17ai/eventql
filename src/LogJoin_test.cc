/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <stdlib.h>
#include <stdio.h>
#include "fnord-base/stdtypes.h"
#include "fnord-base/exception.h"
#include "fnord-base/test/unittest.h"
#include "fnord-msg/msg.h"
#include "src/schemas.cc"
#include "logjoin/LogJoinTarget.cc"
#include "JoinedSession.pb.h"

using namespace fnord;
using namespace cm;

UNIT_TEST(LogJoinTest);

typedef HashMap<String, HashMap<String, String>> StringMap;

LogJoinTarget mkTestTarget() {
  auto schemas = new msg::MessageSchemaRepository(); // FIXPAUL leak
  loadDefaultSchemas(schemas);
  LogJoinTarget trgt(schemas, false);

  trgt.setNormalize([] (Language l, const String& q) { return q; });

  trgt.setGetField([] (const DocID& doc, const String& field) {
    return None<String>();
  });

  return trgt;
}

LogJoinTarget mkTestTargetWithFieldExpansion() {
  auto schemas = new msg::MessageSchemaRepository(); // FIXPAUL leak
  loadDefaultSchemas(schemas);
  LogJoinTarget trgt(schemas, false);

  trgt.setNormalize([] (Language l, const String& q) { return q; });

  trgt.setGetField([] (
      const DocID& doc,
      const String& field) -> Option<String> {
    if (field == "category1") {
      return Some(String("1"));
    }
    if (doc.docid == "p~101") {
      if (field == "category2") {
        return Some(String("21"));
      }
      if (field == "category3") {
        return Some(String("31"));
      }
    }
    if (doc.docid == "p~102") {
      if (field == "category2") {
        return Some(String("22"));
      }
      if (field == "category3") {
        return Some(String("32"));
      }
    }

    return None<String>();
  });

  return trgt;
}

// field expansion // joining

/*
 * Simple Search
 */
TEST_CASE(LogJoinTest, SimpleQuery, [] () {
  auto t = 1432311555 * kMicrosPerSecond;
  auto trgt = mkTestTarget();
  TrackedSession sess;
  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "is", "p~101~p1,p~102~p2" },
    { "qstr~de", "blah" }
  });

  auto buf = trgt.trackedSessionToJoinedSession(sess);
  auto joined = msg::decode<JoinedSession>(buf);
  EXPECT_EQ(joined.num_cart_items(), 0);
  EXPECT_EQ(joined.cart_value_eurcents(), 0);
  EXPECT_EQ(joined.num_order_items(), 0);
  EXPECT_EQ(joined.gmv_eurcents(), 0);
  EXPECT_EQ(joined.first_seen_time(), 1432311555);

  EXPECT_EQ(joined.search_queries().size(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items_clicked(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_clicks(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_cart_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).cart_value_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_order_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).gmv_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).query_string(), "blah");
  EXPECT_EQ(joined.search_queries().Get(0).query_string_normalized(), "blah");
  EXPECT_EQ(
    ProtoLanguage_Name(joined.search_queries().Get(0).language()),
    "LANGUAGE_DE");
  EXPECT_EQ(
    ProtoPageType_Name(joined.search_queries().Get(0).page_type()),
    "PAGETYPE_SEARCH_PAGE");
  //DaWanda Hack
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_impressions(), 2);

  EXPECT_EQ(joined.search_queries().Get(0).result_items().size(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(0).position(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(0).item_id(), "p~101");
  EXPECT_FALSE(joined.search_queries().Get(0).result_items().Get(0).clicked());

  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(1).position(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(1).item_id(), "p~102");
  EXPECT_FALSE(joined.search_queries().Get(0).result_items().Get(1).clicked());
});

/*
 * Cart Item (checkout_step 1) and GMV
 */
TEST_CASE(LogJoinTest, ItemOrder, [] () {
  auto t = 1432311555 * kMicrosPerSecond;
  auto trgt = mkTestTarget();
  TrackedSession sess;
  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "q_cat", "1" },
    { "pg", "1" },
    { "is", "p~105~p5,p~106~p6" }
  });

  sess.insertLogline(t + 10 * kMicrosPerSecond, "v", "E2", URI::ParamList {
    { "i", "p~105"}
  });

  sess.insertLogline(t + 60 * kMicrosPerSecond, "c", "E1", URI::ParamList {
    { "s", "1"},
    { "is", "p~105~2~550~eur" },
  });

  auto buf = trgt.trackedSessionToJoinedSession(sess);
  auto joined = msg::decode<JoinedSession>(buf);
  EXPECT_EQ(joined.num_cart_items(), 1);
  EXPECT_EQ(joined.cart_value_eurcents(), 1100);
  EXPECT_EQ(joined.num_order_items(), 1);
  EXPECT_EQ(joined.gmv_eurcents(), 1100);
  EXPECT_EQ(joined.first_seen_time(), 1432311555);
  EXPECT_EQ(joined.last_seen_time(), 1432311615);

  EXPECT_EQ(joined.cart_items().size(), 1);
  EXPECT_EQ(joined.cart_items().Get(0).time(), 1432311615);
  EXPECT_EQ(joined.cart_items().Get(0).item_id(), "p~105");
  EXPECT_EQ(joined.cart_items().Get(0).quantity(), 2);
  EXPECT_EQ(joined.cart_items().Get(0).price_cents(), 550);
  EXPECT_EQ(
    ProtoCurrency_Name(joined.cart_items().Get(0).currency()),
    "CURRENCY_EUR");
  EXPECT_EQ(joined.cart_items().Get(0).checkout_step(), 1);

  EXPECT_EQ(joined.search_queries().size(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items_clicked(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_clicks(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_cart_items(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).cart_value_eurcents(), 1100);
  EXPECT_EQ(joined.search_queries().Get(0).num_order_items(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).gmv_eurcents(), 1100);
  EXPECT_EQ(joined.search_queries().Get(0).page(), 1);
  EXPECT_EQ(
    ProtoLanguage_Name(joined.search_queries().Get(0).language()),
    "LANGUAGE_UNKNOWN_LANGUAGE");
  EXPECT_EQ(
    ProtoPageType_Name(joined.search_queries().Get(0).page_type()),
    "PAGETYPE_CATALOG_PAGE");
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_impressions(), 0);

  EXPECT_EQ(joined.search_queries().Get(0).result_items().size(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(0).position(), 5);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(0).item_id(), "p~105");
  EXPECT_TRUE(joined.search_queries().Get(0).result_items().Get(0).clicked());

  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(1).position(), 6);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().Get(1).item_id(), "p~106");
  EXPECT_FALSE(joined.search_queries().Get(0).result_items().Get(1).clicked());

  EXPECT_EQ(joined.item_visits().size(), 1);
  EXPECT_EQ(joined.item_visits().Get(0).time(), 1432311565);
  EXPECT_EQ(joined.item_visits().Get(0).item_id(), "p~105");
});


/*
 * Multiple Query Batches and Ad click
 */
TEST_CASE(LogJoinTest, MultipleQueryBatches, [] () {
  auto t = 1432311555 * kMicrosPerSecond;
  auto trgt = mkTestTarget();
  TrackedSession sess;
  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "q_cat", "1" },
    { "pg", "1" },
    { "is", "p~105~p5,p~106~p6,p~107~p7,p~108~p8" }
  });

  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "q_cat", "1" },
    { "pg", "1" },
    { "is", "p~101~p1,p~102~p2,p~103~p3,p~104~p4" }
  });

  sess.insertLogline(t + 10 * kMicrosPerSecond, "v", "E2", URI::ParamList {
    { "i", "p~101"}
  });

  auto buf = trgt.trackedSessionToJoinedSession(sess);
  auto joined = msg::decode<JoinedSession>(buf);

  EXPECT_EQ(joined.num_cart_items(), 0);
  EXPECT_EQ(joined.cart_value_eurcents(), 0);
  EXPECT_EQ(joined.num_order_items(), 0);
  EXPECT_EQ(joined.gmv_eurcents(), 0);
  EXPECT_EQ(joined.first_seen_time(), 1432311555);
  EXPECT_EQ(joined.last_seen_time(), 1432311565);

  EXPECT_EQ(joined.search_queries().size(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items(), 8);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items_clicked(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_impressions(), 4);
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_clicks(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).num_cart_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).cart_value_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_order_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).gmv_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).page(), 1);
  EXPECT_EQ(
    ProtoLanguage_Name(joined.search_queries().Get(0).language()),
    "LANGUAGE_UNKNOWN_LANGUAGE");
  EXPECT_EQ(
    ProtoPageType_Name(joined.search_queries().Get(0).page_type()),
    "PAGETYPE_CATALOG_PAGE");
});


/*
 * Multiple Queries (search, catalog, shop)
 */
TEST_CASE(LogJoinTest, MultipleQueries, [] () {
  auto t = 1432311555 * kMicrosPerSecond;
  auto trgt = mkTestTarget();
  TrackedSession sess;
  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "is", "p~101~p1,p~102~p2" },
    { "qstr~de", "blah" }
  });
  sess.insertLogline(t + 30 * kMicrosPerSecond, "q", "E2", URI::ParamList {
    { "q_cat", "1" },
    { "pg", "1" },
    { "is", "p~105~p5,p~106~p6,p~107~p7,p~108~p8" }
  });
  sess.insertLogline(t + 60 * kMicrosPerSecond, "q", "E3", URI::ParamList {
    { "slrid", "123" },
    { "pg", "1" },
    { "is", "p~11~p1,p~12~p2,p~13~p3,p~14~p4" }
  });
  auto buf = trgt.trackedSessionToJoinedSession(sess);
  auto joined = msg::decode<JoinedSession>(buf);
  EXPECT_EQ(joined.search_queries().size(), 3);
  EXPECT_EQ(joined.search_queries().Get(0).time(), 1432311555);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).num_result_items_clicked(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_impressions(), 2);
  EXPECT_EQ(joined.search_queries().Get(0).num_ad_clicks(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_cart_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).cart_value_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).num_order_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(0).gmv_eurcents(), 0);
  EXPECT_EQ(
    ProtoLanguage_Name(joined.search_queries().Get(0).language()),
    "LANGUAGE_DE");
  EXPECT_EQ(
    ProtoPageType_Name(joined.search_queries().Get(0).page_type()),
    "PAGETYPE_SEARCH_PAGE");

  EXPECT_EQ(joined.search_queries().Get(1).time(), 1432311585);
  EXPECT_EQ(joined.search_queries().Get(1).num_result_items(), 4);
  EXPECT_EQ(joined.search_queries().Get(1).num_result_items_clicked(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).num_ad_impressions(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).num_ad_clicks(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).num_cart_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).cart_value_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).num_order_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).gmv_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(1).page(), 1);
  EXPECT_EQ(
    ProtoLanguage_Name(joined.search_queries().Get(1).language()),
    "LANGUAGE_UNKNOWN_LANGUAGE");
  EXPECT_EQ(
    ProtoPageType_Name(joined.search_queries().Get(1).page_type()),
    "PAGETYPE_CATALOG_PAGE");

  EXPECT_EQ(joined.search_queries().Get(2).time(), 1432311615);
  EXPECT_EQ(joined.search_queries().Get(2).shop_id(), 123);
  EXPECT_EQ(joined.search_queries().Get(2).num_result_items(), 4);
  EXPECT_EQ(joined.search_queries().Get(2).num_result_items_clicked(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).num_ad_impressions(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).num_ad_clicks(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).num_cart_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).cart_value_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).num_order_items(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).gmv_eurcents(), 0);
  EXPECT_EQ(joined.search_queries().Get(2).page(), 1);
  EXPECT_EQ(
    ProtoLanguage_Name(joined.search_queries().Get(2).language()),
    "LANGUAGE_UNKNOWN_LANGUAGE");
  EXPECT_EQ(
    ProtoPageType_Name(joined.search_queries().Get(2).page_type()),
    "PAGETYPE_UNKNOWN_PAGE");
});

/*
 * Field Expansion
 */
TEST_CASE(LogJoinTest, FieldExpansion, [] () {
  auto t = 1432311555 * kMicrosPerSecond;
  auto trgt = mkTestTargetWithFieldExpansion();
  TrackedSession sess;
  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "is", "p~101~p1,p~102~p2" },
    { "qstr~de", "blah" }
  });

  auto buf = trgt.trackedSessionToJoinedSession(sess);
  auto joined = msg::decode<JoinedSession>(buf);

  EXPECT_EQ(joined.search_queries().Get(0).result_items().size(), 2);
  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(0).item_id(),
    "p~101");
  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(0).category1(),
    1);
  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(0).category2(),
    21);
  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(0).category3(),
    31);

  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(1).category1(),
    1);
  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(1).category2(),
    22);
  EXPECT_EQ(
    joined.search_queries().Get(0).result_items().Get(1).category3(),
    32);
});

TEST_CASE(LogJoinTest, SeenResultItems, [] () {
  auto t = 1432311555 * kMicrosPerSecond;
  auto trgt = mkTestTarget();

  TrackedSession sess;
  sess.insertLogline(t + 0, "q", "E1", URI::ParamList {
    { "is", "p~101~p1,p~102~p2,p~103~p3" },
    { "qstr~de", "blah" }
  });

  sess.insertLogline(t + 2, "q", "E1", URI::ParamList {
    { "is", "p~102~p2~s,p~103~s" }
  });

  auto buf = trgt.trackedSessionToJoinedSession(sess);
  auto joined = msg::decode<JoinedSession>(buf);

  EXPECT_EQ(joined.search_queries().size(), 1);
  EXPECT_EQ(joined.search_queries().Get(0).result_items().size(), 3);

  EXPECT_EQ(
      joined.search_queries().Get(0).result_items().Get(0).item_id(),
      "p~101");

  EXPECT_EQ(
      joined.search_queries().Get(0).result_items().Get(0).position(),
      1);

  EXPECT_FALSE(joined.search_queries().Get(0).result_items().Get(0).seen());

  EXPECT_EQ(
      joined.search_queries().Get(0).result_items().Get(1).item_id(),
      "p~102");

  EXPECT_EQ(
      joined.search_queries().Get(0).result_items().Get(1).position(),
      2);

  EXPECT_TRUE(joined.search_queries().Get(0).result_items().Get(1).seen());

  EXPECT_EQ(
      joined.search_queries().Get(0).result_items().Get(2).item_id(),
      "p~103");

  EXPECT_EQ(
      joined.search_queries().Get(0).result_items().Get(2).position(),
      3);

  EXPECT_TRUE(joined.search_queries().Get(0).result_items().Get(2).seen());
});
/*

TEST_CASE(LogJoinTest, Test1, [] () {
  auto loglines_txt = FileUtil::read("src/test_loglines.txt");
  auto loglines = StringUtil::split(loglines_txt.toString(), "\n");

  for (const auto& logline : loglines) {
    if (logline.length() == 0) {
      continue;
    }

    URI::ParamList params;
    URI::parseQueryString(logline, &params);

    String time;
    String evtype;
    String evid;
    for (auto p = params.begin(); p != params.end(); ) {
      switch (p->first[0]) {

        default:
          ++p;
          continue;

        case 't':
          time = p->second;
          break;

        case 'e':
          evtype = p->second;
          break;

        case 'c':
          Vector<String> cid = StringUtil::split(p->second, "~");
          evid = cid.at(1);
          break;
      }

      p = params.erase(p);
    }

    tracked_session.insertLogline(
      DateTime((uint64_t) std::stoull(time) * 1000000),
      evtype,
      evid,
      params);
  }

  auto joined_sessions_schema = joinedSessionsSchema();
  LogJoinTarget logjoin_target(joined_sessions_schema, false);

  StringMap fields;
  fields["p~62797695"]["category1"] = "4";

  auto getField = [fields] (const DocID& docid, const String& feature) -> Option<String> {

    auto it = fields.find(docid.docid);
    if (it != fields.end()) {
      auto field = it->second;
      auto it_ = field.find(feature);
      if (it_ != field.end()) {
        return Some(String(it_->second));
      }
    }

    return None<String>();
  };

  auto normalize = [] (Language lang, const String& query) -> String {
    return query;
  };

  logjoin_target.setGetField(getField);
  logjoin_target.setNormalize(normalize);
  Buffer session = logjoin_target.trackedSessionToJoinedSession(
    tracked_session);

  JoinedSession joined_session;
  joined_session.ParseFromString(session.toString());
});

*/
