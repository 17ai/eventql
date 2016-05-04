/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include "stx/util/binarymessagewriter.h"
#include "eventql/core/TSDBServlet.h"
#include "eventql/core/RecordEnvelope.pb.h"
#include "stx/json/json.h"
#include <stx/wallclock.h>
#include <stx/thread/wakeup.h>
#include "stx/protobuf/MessageEncoder.h"
#include "stx/protobuf/MessagePrinter.h"
#include "stx/protobuf/msg.h"
#include <stx/util/Base64.h>
#include <stx/fnv.h>
#include <eventql/infra/sstable/sstablereader.h>
#include <csql/runtime/ASCIITableFormat.h>
#include <csql/runtime/JSONSSEStreamFormat.h>

using namespace stx;

namespace zbase {

TSDBServlet::TSDBServlet(
    TSDBService* node,
    const String& tmpdir) :
    node_(node),
    tmpdir_(tmpdir) {}

void TSDBServlet::handleHTTPRequest(
    RefPtr<http::HTTPRequestStream> req_stream,
    RefPtr<http::HTTPResponseStream> res_stream) {
  const auto& req = req_stream->request();
  URI uri(req.uri());

  http::HTTPResponse res;
  res.populateFromRequest(req);

  res.addHeader("Access-Control-Allow-Origin", "*");
  res.addHeader("Access-Control-Allow-Methods", "GET, POST");
  res.addHeader("Access-Control-Allow-Headers", "X-TSDB-Namespace");

  if (req.method() == http::HTTPMessage::M_OPTIONS) {
    req_stream->readBody();
    res.setStatus(http::kStatusOK);
    res_stream->writeResponse(res);
    return;
  }

  try {
    if (uri.path() == "/tsdb/insert") {
      req_stream->readBody();
      insertRecords(&req, &res, &uri);
      res_stream->writeResponse(res);
      return;
    }

    if (uri.path() == "/tsdb/replicate") {
      req_stream->readBody();
      replicateRecords(&req, &res, &uri);
      res_stream->writeResponse(res);
      return;
    }

    if (uri.path() == "/tsdb/compact") {
      req_stream->readBody();
      compactPartition(&req, &res, &uri);
      res_stream->writeResponse(res);
      return;
    }

    if (uri.path() == "/tsdb/stream") {
      req_stream->readBody();
      streamPartition(&req, &res, res_stream, &uri);
      return;
    }

    if (uri.path() == "/tsdb/partition_info") {
      req_stream->readBody();
      fetchPartitionInfo(&req, &res, &uri);
      res_stream->writeResponse(res);
      return;
    }

    if (uri.path() == "/tsdb/sql") {
      req_stream->readBody();
      executeSQL(&req, &res, &uri);
      res_stream->writeResponse(res);
      return;
    }

    if (uri.path() == "/tsdb/sql_stream") {
      req_stream->readBody();
      executeSQLStream(&req, &res, res_stream, &uri);
      return;
    }

    if (uri.path() == "/tsdb/update_cstable") {
      updateCSTable(uri, req_stream.get(), &res);
      res_stream->writeResponse(res);
      return;
    }

    res.setStatus(stx::http::kStatusNotFound);
    res.addBody("not found");
    res_stream->writeResponse(res);
  } catch (const Exception& e) {
    stx::logError("tsdb", e, "error while processing HTTP request");

    res.setStatus(http::kStatusInternalServerError);
    res.addBody(StringUtil::format("error: $0: $1", e.getTypeName(), e.getMessage()));
    res_stream->writeResponse(res);
  }

  res_stream->finishResponse();
}

void TSDBServlet::insertRecords(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    URI* uri) {
  auto record_list = msg::decode<RecordEnvelopeList>(req->body());
  node_->insertRecords(record_list);
  res->setStatus(http::kStatusCreated);
}

void TSDBServlet::compactPartition(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    URI* uri) {
  const auto& params = uri->queryParams();

  String tsdb_namespace;
  if (!URI::getParam(params, "namespace", &tsdb_namespace)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?namespace=... parameter");
    return;
  }

  String table_name;
  if (!URI::getParam(params, "table", &table_name)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?table=... parameter");
    return;
  }

  String partition_key;
  if (!URI::getParam(params, "partition", &partition_key)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?partition=... parameter");
    return;
  }

  node_->compactPartition(
      tsdb_namespace,
      table_name,
      SHA1Hash::fromHexString(partition_key));

  res->setStatus(http::kStatusCreated);
}

void TSDBServlet::replicateRecords(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    URI* uri) {
  auto record_list = msg::decode<RecordEnvelopeList>(req->body());
  auto insert_flags = (uint64_t) InsertFlags::REPLICATED_WRITE;
  if (record_list.sync_commit()) {
    insert_flags |= (uint64_t) InsertFlags::SYNC_COMMIT;
  }

  node_->insertRecords(record_list, insert_flags);
  res->setStatus(http::kStatusCreated);
}

void TSDBServlet::streamPartition(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    RefPtr<http::HTTPResponseStream> res_stream,
    URI* uri) {
  const auto& params = uri->queryParams();

  String tsdb_namespace;
  if (!URI::getParam(params, "namespace", &tsdb_namespace)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?namespace=... parameter");
    res_stream->writeResponse(*res);
    return;
  }

  String table_name;
  if (!URI::getParam(params, "stream", &table_name)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?stream=... parameter");
    res_stream->writeResponse(*res);
    return;
  }

  String partition_key;
  if (!URI::getParam(params, "partition", &partition_key)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?partition=... parameter");
    res_stream->writeResponse(*res);
    return;
  }

  size_t sample_mod = 0;
  size_t sample_idx = 0;
  String sample_str;
  if (URI::getParam(params, "sample", &sample_str)) {
    auto parts = StringUtil::split(sample_str, ":");

    if (parts.size() != 2) {
      res->setStatus(stx::http::kStatusBadRequest);
      res->addBody("invalid ?sample=... parameter, format is <mod>:<idx>");
      res_stream->writeResponse(*res);
    }

    sample_mod = std::stoull(parts[0]);
    sample_idx = std::stoull(parts[1]);
  }

  res->setStatus(http::kStatusOK);
  res->addHeader("Content-Type", "application/octet-stream");
  res->addHeader("Connection", "close");
  res_stream->startResponse(*res);

  node_->fetchPartitionWithSampling(
      tsdb_namespace,
      table_name,
      SHA1Hash::fromHexString(partition_key),
      sample_mod,
      sample_idx,
      [&res_stream] (const Buffer& record) {
    util::BinaryMessageWriter buf;

    if (record.size() > 0) {
      buf.appendUInt64(record.size());
      buf.append(record.data(), record.size());
      res_stream->writeBodyChunk(Buffer(buf.data(), buf.size()));
    }

    res_stream->waitForReader();
  });

  util::BinaryMessageWriter buf;
  buf.appendUInt64(0);
  res_stream->writeBodyChunk(Buffer(buf.data(), buf.size()));

  res_stream->finishResponse();
}

void TSDBServlet::fetchPartitionInfo(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    URI* uri) {
  const auto& params = uri->queryParams();

  String tsdb_namespace;
  if (!URI::getParam(params, "namespace", &tsdb_namespace)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?namespace=... parameter");
    return;
  }

  String table_name;
  if (!URI::getParam(params, "stream", &table_name)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?stream=... parameter");
    return;
  }

  String partition_key;
  if (!URI::getParam(params, "partition", &partition_key)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?partition=... parameter");
    return;
  }

  auto pinfo = node_->partitionInfo(
      tsdb_namespace,
      table_name,
      SHA1Hash::fromHexString(partition_key));

  if (pinfo.isEmpty()) {
    res->setStatus(http::kStatusNotFound);
  } else {
    res->setStatus(http::kStatusOK);
    res->addHeader("Content-Type", "application/x-protobuf");
    res->addBody(*msg::encode(pinfo.get()));
  }
}

void TSDBServlet::executeSQL(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    URI* uri) {
  auto tsdb_namespace = req->getHeader("X-TSDB-Namespace");
  auto query = req->body().toString();

  Buffer result;
  //node_->sqlEngine()->executeQuery(
  //    tsdb_namespace,
  //    query,
  //    new csql::ASCIITableFormat(BufferOutputStream::fromBuffer(&result)));

  res->setStatus(http::kStatusOK);
  res->addHeader("Content-Type", "text/plain");
  res->addHeader("Connection", "close");
  res->addBody(result);
}

void TSDBServlet::executeSQLStream(
    const http::HTTPRequest* req,
    http::HTTPResponse* res,
    RefPtr<http::HTTPResponseStream> res_stream,
    URI* uri) {
  http::HTTPSSEStream sse_stream(res, res_stream);
  sse_stream.start();

  try {
    const auto& params = uri->queryParams();

    String tsdb_namespace;
    if (!URI::getParam(params, "namespace", &tsdb_namespace)) {
      RAISE(kRuntimeError, "missing ?namespace=... parameter");
    }

    String query;
    if (!URI::getParam(params, "query", &query)) {
      RAISE(kRuntimeError, "missing ?query=... parameter");
    }

    //node_->sqlEngine()->executeQuery(
    //    tsdb_namespace,
    //    query,
    //    new csql::JSONSSEStreamFormat(&sse_stream));

  } catch (const StandardException& e) {
    stx::logError("sql", e, "SQL execution failed");

    Buffer buf;
    json::JSONOutputStream json(BufferOutputStream::fromBuffer(&buf));
    json.beginObject();
    json.addObjectEntry("error");
    json.addString(e.what());
    json.endObject();

    sse_stream.sendEvent(buf, Some(String("error")));
  }

  sse_stream.finish();
}

void TSDBServlet::updateCSTable(
    const URI& uri,
    http::HTTPRequestStream* req_stream,
    http::HTTPResponse* res) {
  const auto& params = uri.queryParams();

  String tsdb_namespace;
  if (!URI::getParam(params, "namespace", &tsdb_namespace)) {
    RAISE(kRuntimeError, "missing ?namespace=... parameter");
  }

  String table_name;
  if (!URI::getParam(params, "table", &table_name)) {
    res->setStatus(http::kStatusBadRequest);
    res->addBody("error: missing ?table=... parameter");
    return;
  }

  String partition_key;
  if (!URI::getParam(params, "partition", &partition_key)) {
    res->setStatus(stx::http::kStatusBadRequest);
    res->addBody("missing ?partition=... parameter");
    return;
  }

  String version;
  if (!URI::getParam(params, "version", &version)) {
    res->setStatus(http::kStatusBadRequest);
    res->addBody("error: missing ?version=... parameter");
    return;
  }

  auto tmpfile_path = FileUtil::joinPaths(
      tmpdir_,
      StringUtil::format("upload_$0.tmp", Random::singleton()->hex128()));

  {
    auto tmpfile = File::openFile(
        tmpfile_path,
        File::O_CREATE | File::O_READ | File::O_WRITE);

    req_stream->readBody([&tmpfile] (const void* data, size_t size) {
      tmpfile.write(data, size);
    });
  }

  node_->updatePartitionCSTable(
      tsdb_namespace,
      table_name,
      SHA1Hash::fromHexString(partition_key),
      tmpfile_path,
      std::stoull(version));

  res->setStatus(http::kStatusCreated);
}



}

