/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include "stx/stdtypes.h"
#include "stx/application.h"
#include "stx/logging.h"
#include "stx/cli/flagparser.h"
#include "stx/io/fileutil.h"
#include "stx/http/httprouter.h"
#include "stx/http/httpserver.h"
#include "stx/thread/eventloop.h"
#include "stx/thread/threadpool.h"
#include "common/CustomerDirectory.h"
#include "master/CustomerDirectoryServlet.h"

using namespace stx;
using namespace cm;

stx::thread::EventLoop ev;

int main(int argc, const char** argv) {
  stx::Application::init();
  stx::Application::logToStderr();

  stx::cli::FlagParser flags;

  flags.defineFlag(
      "http_port",
      stx::cli::FlagParser::T_INTEGER,
      false,
      NULL,
      "7003",
      "Start the public http server on this port",
      "<port>");

  flags.defineFlag(
      "datadir",
      cli::FlagParser::T_STRING,
      true,
      NULL,
      NULL,
      "datadir path",
      "<path>");

  flags.defineFlag(
      "loglevel",
      stx::cli::FlagParser::T_STRING,
      false,
      NULL,
      "INFO",
      "loglevel",
      "<level>");

  flags.parseArgv(argc, argv);

  Logger::get()->setMinimumLogLevel(
      strToLogLevel(flags.getString("loglevel")));

  /* thread pools */
  stx::thread::ThreadPool tpool;

  /* http */
  stx::http::HTTPRouter http_router;
  stx::http::HTTPServer http_server(&http_router, &ev);
  http_server.listen(flags.getInt("http_port"));

  /* customer directory */
  auto cdb_dir = FileUtil::joinPaths(flags.getString("datadir"), "cdb");
  if (!FileUtil::exists(cdb_dir)) {
    FileUtil::mkdir(cdb_dir);
  }

  CustomerDirectory customer_dir(cdb_dir);
  CustomerDirectoryServlet customer_dir_servlet(&customer_dir);
  http_router.addRouteByPrefixMatch("/cdb", &customer_dir_servlet, &tpool);

  stx::logInfo("dxa-master", "Exiting...");

  exit(0);
}

