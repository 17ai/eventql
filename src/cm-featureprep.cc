/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include "fnord/io/filerepository.h"
#include "fnord/io/fileutil.h"
#include "fnord/io/mmappedfile.h"
#include "fnord/application.h"
#include "fnord/logging.h"
#include "fnord/random.h"
#include "fnord/thread/eventloop.h"
#include "fnord/thread/threadpool.h"
#include "fnord/wallclock.h"
#include "fnord/util/SimpleRateLimit.h"
#include "fnord-rpc/ServerGroup.h"
#include "fnord-rpc/RPC.h"
#include "fnord-rpc/RPCClient.h"
#include "fnord/cli/flagparser.h"
#include "fnord/json/json.h"
#include "fnord/json/jsonrpc.h"
#include "fnord/http/httprouter.h"
#include "fnord/http/httpserver.h"
#include "fnord-sstable/sstablereader.h"
#include "fnord-sstable/sstablewriter.h"
#include "fnord-sstable/SSTableColumnSchema.h"
#include "fnord-sstable/SSTableColumnReader.h"
#include "fnord-sstable/SSTableColumnWriter.h"
#include "fnord-feeds/FeedService.h"
#include "fnord-feeds/RemoteFeedFactory.h"
#include "fnord-feeds/RemoteFeedReader.h"
#include "fnord/stats/statsdagent.h"
#include "fnord/RadixTree.h"
#include "fnord/mdb/MDB.h"
#include <fnord-fts/fts.h>
#include <fnord-fts/fts_common.h>
#include "common.h"
#include "CustomerNamespace.h"


#include "FeatureIndex.h"
#include "FeatureSelector.h"
#

using namespace fnord;

int main(int argc, const char** argv) {
  fnord::Application::init();
  fnord::Application::logToStderr();

  fnord::cli::FlagParser flags;

  flags.defineFlag(
      "file",
      fnord::cli::FlagParser::T_STRING,
      true,
      "i",
      NULL,
      "file",
      "<filename>");

  flags.defineFlag(
      "featuredb_path",
      cli::FlagParser::T_STRING,
      true,
      NULL,
      NULL,
      "feature db path",
      "<path>");

  flags.defineFlag(
      "conf",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      "./conf",
      "conf directory",
      "<path>");

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

  auto file = flags.getString("file");
  auto file_prefix = file;
  StringUtil::replaceAll(&file_prefix, ".sstable", "");
  auto output_data_file = file_prefix + "_features.sstable";
  auto output_meta_file = file_prefix + "_features_meta.sstable";
  auto eligibility = cm::ItemEligibility::DAWANDA_ALL_NOBOTS;

  /* set up feature schema */
  cm::FeatureSchema feature_schema;
  feature_schema.registerFeature("shop_id", 1, 1);
  feature_schema.registerFeature("category1", 2, 1);
  feature_schema.registerFeature("category2", 3, 1);
  feature_schema.registerFeature("category3", 4, 1);
  feature_schema.registerFeature("title~de", 5, 2);

  /* set up output sstable schemas */
  sstable::SSTableColumnSchema sstable_schema;
  sstable_schema.addColumn("label", 1, sstable::SSTableColumnType::FLOAT);
  sstable_schema.addColumn("feature", 2, sstable::SSTableColumnType::STRING);

  sstable::SSTableColumnSchema meta_sstable_schema;
  meta_sstable_schema.addColumn("views", 1, sstable::SSTableColumnType::UINT64);
  meta_sstable_schema.addColumn("clicks", 2, sstable::SSTableColumnType::UINT64);

  /* open featuredb */
  auto featuredb_path = flags.getString("featuredb_path");
  auto featuredb = mdb::MDB::open(featuredb_path, true);
  cm::FeatureIndex feature_index(featuredb, &feature_schema);

  /* set up analyzer */
  fnord::fts::Analyzer analyzer(flags.getString("conf"));

  /* feature selector */
  cm::FeatureSelector feature_select(&feature_index, &analyzer);
  HashMap<String, Pair<uint32_t, uint32_t>> feature_counts;

  /* open output sstable */
  fnord::logInfo(
      "cm.featureprep",
      "Writing features to: $0",
      output_data_file);

  fnord::logInfo(
      "cm.featureprep",
      "Writing feature metadata to: $0",
      output_meta_file);

  if (FileUtil::exists(output_data_file + "~")) {
    fnord::logInfo(
        "cm.featureprep",
        "Deleting orphaned tmp file: $0",
        output_data_file + "~");

    FileUtil::rm(output_data_file + "~");
  }

  if (FileUtil::exists(output_meta_file + "~")) {
    fnord::logInfo(
        "cm.featureprep",
        "Deleting orphaned tmp file: $0",
        output_meta_file + "~");

    FileUtil::rm(output_meta_file + "~");
  }

  auto sstable_writer = sstable::SSTableWriter::create(
      output_data_file + "~",
      sstable::IndexProvider{},
      NULL,
      0);

  /* read input table */
  fnord::logInfo("cm.featureprep", "Importing sstable: $0", file);
  sstable::SSTableReader reader(File::openFile(file, File::O_READ));

  if (reader.bodySize() == 0) {
    fnord::logCritical("cm.featureprep", "unfinished table: $0", file);
    return 1;
  }

  /* get sstable cursor */
  auto cursor = reader.getCursor();
  auto body_size = reader.bodySize();
  int row_idx = 0;

  /* status line */
  util::SimpleRateLimitedFn status_line(kMicrosPerSecond, [&] () {
    fnord::logInfo(
        "cm.featureprep",
        "[$0%] Reading sstable... rows=$1",
        (size_t) ((cursor->position() / (double) body_size) * 100),
        row_idx);
  });

  /* read sstable rows */
  for (; cursor->valid(); ++row_idx) {
    status_line.runMaybe();

    auto val = cursor->getDataBuffer();

    try {
      auto q = json::fromJSON<cm::JoinedQuery>(val);

      if (isQueryEligible(eligibility, q)) {
        for (const auto& item : q.items) {
          if (!isItemEligible(eligibility, q, item)) {
            continue;
          }

          double label = item.clicked ? 1.0 : -1.0;

          Set<String> features;
          feature_select.featuresFor(q, item, &features);

          sstable::SSTableColumnWriter cols(&sstable_schema);
          cols.addFloatColumn(1, label);

          for (const auto& f : features) {
            cols.addStringColumn(2, f);
            ++feature_counts[f].first;
            if (item.clicked) {
              ++feature_counts[f].second;
            }
          }

          sstable_writer->appendRow("", cols);
        }
      }
    } catch (const Exception& e) {
      fnord::logWarning(
          "cm.featureprep",
          e,
          "error while indexing query: $0",
          val.toString());
    }

    if (!cursor->next()) {
      break;
    }
  }

  status_line.runForce();

  sstable_schema.writeIndex(sstable_writer.get());
  sstable_writer->finalize();

  /* write meta sstable */
  auto meta_sstable_writer = sstable::SSTableWriter::create(
      output_meta_file + "~",
      sstable::IndexProvider{},
      NULL,
      0);

  for (const auto& f : feature_counts) {
    sstable::SSTableColumnWriter cols(&meta_sstable_schema);
    cols.addUInt64Column(1, f.second.first);
    cols.addUInt64Column(2, f.second.second);
    meta_sstable_writer->appendRow(f.first, cols);
  }

  meta_sstable_schema.writeIndex(meta_sstable_writer.get());
  meta_sstable_writer->finalize();

  FileUtil::mv(output_meta_file + "~", output_meta_file);
  FileUtil::mv(output_data_file + "~", output_data_file);

  return 0;
}

