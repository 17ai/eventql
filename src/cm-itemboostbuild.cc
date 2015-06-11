/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <fnord/stdtypes.h>
#include <fnord/cli/flagparser.h>
#include <fnord/application.h>
#include <fnord/logging.h>
#include <fnord/random.h>
#include <fnord/wallclock.h>
#include <fnord/thread/eventloop.h>
#include <fnord/thread/threadpool.h>
#include <fnord/http/httpconnectionpool.h>
#include <dproc/Application.h>
#include <dproc/LocalScheduler.h>
#include <fnord-tsdb/TSDBClient.h>
#include "analytics/ItemBoostMapper.h"
#include "analytics/ItemBoostMerge.h"
#include "analytics/ItemBoostExport.h"
#include "analytics/ProtoSSTableMergeReducer.h"

using namespace fnord;
using namespace cm;

fnord::thread::EventLoop ev;

int main(int argc, const char** argv) {
  fnord::Application::init();
  fnord::Application::logToStderr();

  fnord::cli::FlagParser flags;

  flags.defineFlag(
      "output",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "output file",
      "<path>");

  flags.defineFlag(
      "tempdir",
      cli::FlagParser::T_STRING,
      false,
      NULL,
      NULL,
      "artifact directory",
      "<path>");

  flags.defineFlag(
      "threads",
      cli::FlagParser::T_INTEGER,
      true,
      NULL,
      "8",
      "nthreads",
      "<num>");

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

  /* start event loop */
  auto evloop_thread = std::thread([] {
    ev.run();
  });

  http::HTTPConnectionPool http(&ev);
  tsdb::TSDBClient tsdb("http://nue03.prod.fnrd.net:7003/tsdb", &http);

  dproc::DefaultApplication app("cm.itemboost");

  app.registerProtoRDDFactory<AnalyticsTableScanMapperParams>(
      "ItemBoostMapper",
      [&tsdb] (const AnalyticsTableScanMapperParams& params)
          -> RefPtr<dproc::Task> {
        auto report = new ItemBoostMapper(
            new AnalyticsTableScanSource(params, &tsdb),
            new ProtoSSTableSink<ItemBoostRow>());

        report->setCacheKey(
            "cm.itemboost~" + report->input()->cacheKey());

        return report;
      });

  app.registerProtoRDDFactory<AnalyticsTableScanReducerParams>(
      "ItemBoostReducer",
      [&tsdb] (const AnalyticsTableScanReducerParams& params)
          -> RefPtr<dproc::Task> {
        auto stream = "joined_sessions." + params.customer();
        auto partitions = tsdb.listPartitions(
            stream,
            params.from_unixmicros(),
            params.until_unixmicros());

        List<dproc::TaskDependency> map_chunks;
        for (const auto& part : partitions) {
          AnalyticsTableScanMapperParams map_chunk_params;
          map_chunk_params.set_stream_key(stream);
          map_chunk_params.set_partition_key(part);

          map_chunks.emplace_back(dproc::TaskDependency {
            .task_name = "ItemBoostMapper",
            .params = *msg::encode(map_chunk_params)
          });
        }

        return new ProtoSSTableMergeReducer<ItemBoostRow>(
            new ProtoSSTableSource<ItemBoostRow>(map_chunks),
            new ProtoSSTableSink<ItemBoostRow>(),
            std::bind(
                &ItemBoostMerge::merge,
                std::placeholders::_1,
                std::placeholders::_2));
      });

  app.registerTaskFactory(
      "ItemBoostExport",
      [&tsdb] (const Buffer& params)
          -> RefPtr<dproc::Task> {
        List<dproc::TaskDependency> deps;
        deps.emplace_back(dproc::TaskDependency {
          .task_name = "ItemBoostReducer",
          .params = params
        });

        return new ItemBoostExport(
            new ProtoSSTableSource<ItemBoostRow>(deps),
            new CSVSink());
      });

  dproc::LocalScheduler sched(flags.getString("tempdir"), flags.getInt("threads"));
  sched.start();

  AnalyticsTableScanReducerParams params;
  params.set_customer("dawanda");
  params.set_from_unixmicros(WallClock::unixMicros() - 30 * kMicrosPerDay);
  params.set_until_unixmicros(WallClock::unixMicros() - 2 * kMicrosPerDay);

  auto res = sched.run(&app, "ItemBoostExport", *msg::encode(params));

  auto output_file = File::openFile(
      flags.getString("output"),
      File::O_CREATEOROPEN | File::O_WRITE | File::O_TRUNCATE);

  output_file.write(res->data(), res->size());

  sched.stop();
  ev.shutdown();
  evloop_thread.join();
  exit(0);
}

