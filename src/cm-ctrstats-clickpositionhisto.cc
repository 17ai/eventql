/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "fnord-base/io/fileutil.h"
#include "fnord-base/application.h"
#include "fnord-base/logging.h"
#include "fnord-base/cli/flagparser.h"
#include "fnord-base/util/SimpleRateLimit.h"
#include "fnord-base/InternMap.h"
#include "fnord-json/json.h"
#include "fnord-mdb/MDB.h"
#include "fnord-mdb/MDBUtil.h"
#include "fnord-sstable/sstablereader.h"
#include "fnord-sstable/sstablewriter.h"
#include "fnord-sstable/SSTableColumnSchema.h"
#include "fnord-sstable/SSTableColumnReader.h"
#include "fnord-sstable/SSTableColumnWriter.h"
#include "common.h"
#include "CustomerNamespace.h"
#include "FeatureSchema.h"
#include "JoinedQuery.h"
#include "CTRCounter.h"

using namespace fnord;

struct PosiInfo {
  PosiInfo() : views(0), clicks(0) {}
  uint64_t views;
  uint64_t clicks;
};

int main(int argc, const char** argv) {
  fnord::Application::init();
  fnord::Application::logToStderr();

  fnord::cli::FlagParser flags;

  flags.defineFlag(
      "loglevel",
      fnord::cli::FlagParser::T_STRING,
      false,
      NULL,
      "INFO",
      "loglevel",
      "<level>");

  flags.parseArgv(argc, argv);

  Logger::get()->setMinimumLogLevel(
      strToLogLevel(flags.getString("loglevel")));

  HashMap<uint32_t, PosiInfo> click_posis;

  auto start_time = std::numeric_limits<uint64_t>::max();
  auto end_time = std::numeric_limits<uint64_t>::min();
  auto eligibility = cm::ItemEligibility::DAWANDA_ALL_NOBOTS;

  util::SimpleRateLimitedFn print_results(kMicrosPerSecond * 10, [&] () {
    uint64_t total_clicks = 0;
    uint64_t total_views = 0;
    Vector<Pair<uint64_t, PosiInfo>> posis;
    for (const auto& p : click_posis) {
      total_clicks += p.second.clicks;
      total_views += p.second.views;
      posis.emplace_back(p);
    }

    std::sort(posis.begin(), posis.end(), [] (
        const Pair<uint64_t, PosiInfo>& a,
        const Pair<uint64_t, PosiInfo>& b) {
      return a.first < b.first;
    });

    for (const auto& p : posis) {
      auto share = (p.second.clicks / (double) total_clicks) * 100;
      auto ctr = p.second.clicks / (double) p.second.views;

      fnord::iputs("  position $0 => views=$1 clicks=$2 share=$3 ctr=$4",
          p.first,
          p.second.views,
          p.second.clicks,
          share,
          ctr);
    }
  });

  /* read input tables */
  auto sstables = flags.getArgv();
  int row_idx = 0;
  for (int tbl_idx = 0; tbl_idx < sstables.size(); ++tbl_idx) {
    const auto& sstable = sstables[tbl_idx];
    fnord::logInfo("cm.ctrstats", "Importing sstable: $0", sstable);

    /* read sstable header */
    sstable::SSTableReader reader(File::openFile(sstable, File::O_READ));

    if (reader.bodySize() == 0) {
      fnord::logCritical("cm.ctrstats", "unfinished sstable: $0", sstable);
      exit(1);
    }

    /* read report header */
    auto hdr = json::parseJSON(reader.readHeader());

    auto tbl_start_time = json::JSONUtil::objectGetUInt64(
        hdr.begin(),
        hdr.end(),
        "start_time").get();

    auto tbl_end_time = json::JSONUtil::objectGetUInt64(
        hdr.begin(),
        hdr.end(),
        "end_time").get();

    if (tbl_start_time < start_time) {
      start_time = tbl_start_time;
    }

    if (tbl_end_time > end_time) {
      end_time = tbl_end_time;
    }

    /* get sstable cursor */
    auto cursor = reader.getCursor();
    auto body_size = reader.bodySize();

    /* status line */
    util::SimpleRateLimitedFn status_line(kMicrosPerSecond, [&] () {
      fnord::logInfo(
          "cm.ctrstats",
          "[$1/$2] [$0%] Reading sstable... rows=$3",
          (size_t) ((cursor->position() / (double) body_size) * 100),
          tbl_idx + 1, sstables.size(), row_idx);
    });

    /* read sstable rows */
    for (; cursor->valid(); ++row_idx) {
      status_line.runMaybe();
      print_results.runMaybe();

      auto val = cursor->getDataBuffer();
      Option<cm::JoinedQuery> q;

      try {
        q = Some(json::fromJSON<cm::JoinedQuery>(val));
      } catch (const Exception& e) {
        //fnord::logWarning("cm.ctrstats", e, "invalid json: $0", val.toString());
      }

      if (!q.isEmpty() && isQueryEligible(eligibility, q.get())) {
        for (auto& item : q.get().items) {
          if (!isItemEligible(eligibility, q.get(), item) ||
              item.position < 1) {
            continue;
          }

          auto& pi = click_posis[item.position];
          ++pi.views;
          pi.clicks += item.clicked;
        }
      }

      if (!cursor->next()) {
        break;
      }
    }

    status_line.runForce();
  }

  print_results.runForce();
  return 0;
}

