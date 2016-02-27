/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <unistd.h>
#include "zbase/api/LogfileService.h"
#include "stx/RegExp.h"
#include "stx/human.h"
#include "stx/protobuf/msg.h"
#include "stx/protobuf/MessageSchema.h"
#include "stx/protobuf/MessagePrinter.h"
#include "stx/protobuf/MessageEncoder.h"
#include "stx/protobuf/DynamicMessage.h"
#include "csql/qtree/SelectListNode.h"
#include "csql/qtree/ColumnReferenceNode.h"
#include "csql/CSTableScan.h"
#include "zbase/core/TimeWindowPartitioner.h"
#include "zbase/core/SQLEngine.h"

using namespace stx;

namespace zbase {

LogfileService::LogfileService(
    ConfigDirectory* cdir,
    AnalyticsAuth* auth,
    zbase::TSDBService* tsdb,
    zbase::PartitionMap* pmap,
    zbase::ReplicationScheme* repl,
    csql::Runtime* sql) :
    cdir_(cdir),
    auth_(auth),
    tsdb_(tsdb),
    pmap_(pmap),
    repl_(repl),
    sql_(sql) {}

void LogfileService::insertLoglines(
    const String& customer,
    const String& logfile_name,
    const Vector<Pair<String, String>>& source_fields,
    InputStream* is) {
  auto logfile_definition = findLogfileDefinition(customer, logfile_name);
  if (logfile_definition.isEmpty()) {
    RAISEF(kNotFoundError, "logfile not found: $0", logfile_name);
  }

  insertLoglines(
      customer,
      logfile_definition.get(),
      source_fields,
      is);
}

void LogfileService::insertLoglines(
    const String& customer,
    const LogfileDefinition& logfile,
    const Vector<Pair<String, String>>& source_fields,
    InputStream* is) {
  String line;

  auto table_name = "logs." + logfile.name();
  auto schema = tsdb_->tableSchema(customer, table_name);
  auto partitioner = tsdb_->tablePartitioner(customer, table_name);
  if (schema.isEmpty() || partitioner.isEmpty()) {
    RAISEF(kNotFoundError, "table not found: $0", table_name);
  }

  msg::DynamicMessage row_base(schema.get());
  for (const auto& f : source_fields) {
    row_base.addField(f.first, f.second);
  }

  RegExp regex(logfile.regex());

  HashMap<size_t, LogfileField> match_fields;
  size_t time_idx = -1;
  String time_format;
  for (const auto& f : logfile.row_fields()) {
    auto match_idx = regex.getNamedCaptureIndex(f.name());
    if (match_idx != size_t(-1)) {
      match_fields.emplace(match_idx, f);
      if (f.name() == "time") {
        time_idx = match_idx;
        time_format = f.format();
      }
    }
  }

  if (time_idx == size_t(-1)) {
    RAISE(kIllegalStateError, "can't import logfile row without time column");
  }

  Vector<RecordEnvelope> records;
  static const size_t kInsertBatchSize = 1024;

  for (; is->readLine(&line); line.clear()) {
    Vector<Pair<const char*, size_t>> match;
    if (!regex.match(line, &match)) {
      logTrace(
          "z1.logs",
          "Dropping logline for logfile '$0/$1' because it does not match " \
          "the parsing regex: $2",
          customer,
          logfile.name(),
          line);

      continue;
    }

    Option<UnixTime> time;
    if (time_format.empty()) {
      time = Human::parseTime(
          String(match[time_idx].first, match[time_idx].second));
    } else {
      time = UnixTime::parseString(
          match[time_idx].first,
          match[time_idx].second,
          time_format.c_str());
    }

    if (time.isEmpty()) {
      logTrace(
          "z1.logs",
          "Dropping logline for logfile '$0/$1' because it does not have a " \
          "'time' column: $2",
          customer,
          logfile.name(),
          line);

      continue;
    }

    auto row = row_base;
    row.addField("raw", line);

    for (size_t i = 0; i < match.size(); ++i) {
      auto mfield = match_fields.find(i);
      if (mfield == match_fields.end()) {
        continue;
      }

      if (mfield->second.has_format() &&
          mfield->second.type() == "DATETIME") {
        auto t = UnixTime::parseString(
            match[i].first,
            match[i].second,
            mfield->second.format().c_str());

        if (!t.isEmpty()) {
          row.addDateTimeField(mfield->second.id(), t.get());
        }
        continue;
      }

      row.addField(
          mfield->second.id(),
          String(match[i].first, match[i].second));
    }

    Buffer row_buf;
    msg::MessageEncoder::encode(row.data(), *row.schema(), &row_buf);

    auto record_id = Random::singleton()->sha1();
    auto partition_key = partitioner.get()->partitionKeyFor(
        StringUtil::toString(time.get().unixMicros()));

    RecordEnvelope envelope;
    envelope.set_tsdb_namespace(customer);
    envelope.set_table_name(table_name);
    envelope.set_partition_sha1(partition_key.toString());
    envelope.set_record_id(record_id.toString());
    envelope.set_record_data(row_buf.toString());
    records.emplace_back(envelope);

    if (records.size() > kInsertBatchSize) {
      tsdb_->insertRecords(records);
      records.clear();
    }
  }

  if (records.size() > 0) {
    tsdb_->insertRecords(records);
  }
}

Option<LogfileDefinition> LogfileService::findLogfileDefinition(
    const String& customer,
    const String& logfile_name) {
  auto cconf = cdir_->configFor(customer);

  for (const auto& logfile : cconf->config.logfile_import_config().logfiles()) {
    if (logfile.name() == logfile_name) {
      return Some(logfile);
    }
  }

  return None<LogfileDefinition>();
}

void LogfileService::setLogfileRegex(
    const String& customer,
    const String& logfile_name,
    const String& regex) {
  auto cconf = cdir_->configFor(customer)->config;
  auto logfile_conf = cconf.mutable_logfile_import_config();

  for (auto& logfile : *logfile_conf->mutable_logfiles()) {
    if (logfile.name() == logfile_name) {
      logfile.set_regex(regex);
      cdir_->updateCustomerConfig(cconf);
      return;
    }
  }

  RAISEF(kNotFoundError, "logfile not found: $0", logfile_name);
}

RefPtr<msg::MessageSchema> LogfileService::getSchema(
    const LogfileDefinition& cfg) {
  Vector<msg::MessageSchemaField> fields;

  fields.emplace_back(
      msg::MessageSchemaField(
          1,
          "raw",
          msg::FieldType::STRING,
          0,
          false,
          true));

  for (const auto& field : cfg.source_fields()) {
    fields.emplace_back(
        msg::MessageSchemaField(
            field.id(),
            field.name(),
            msg::fieldTypeFromString(field.type()),
            0,
            false,
            true));
  }

  for (const auto& field : cfg.row_fields()) {
    fields.emplace_back(
        msg::MessageSchemaField(
            field.id(),
            field.name(),
            msg::fieldTypeFromString(field.type()),
            0,
            false,
            true));
  }

  return new msg::MessageSchema(cfg.name(), fields);
}

Vector<TableDefinition> LogfileService::getTableDefinitions(
    const CustomerConfig& cfg) {
  Vector<TableDefinition> tbls;

  if (!cfg.has_logfile_import_config()) {
    return tbls;
  }

  for (const auto& logfile : cfg.logfile_import_config().logfiles()) {
    TableDefinition td;
    td.set_customer(cfg.customer());
    td.set_table_name("logs." + logfile.name());
    auto tblcfg = td.mutable_config();
    tblcfg->set_schema(getSchema(logfile)->encode().toString());
    tblcfg->set_partitioner(zbase::TBL_PARTITION_TIMEWINDOW);
    tblcfg->set_storage(zbase::TBL_STORAGE_COLSM);

    auto partcfg = tblcfg->mutable_time_window_partitioner_config();
    partcfg->set_partition_size(10 * kMicrosPerMinute);

    tbls.emplace_back(td);
  }

  return tbls;
}

} // namespace zbase
