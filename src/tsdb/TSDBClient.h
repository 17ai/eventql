/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef _FNORD_TSDB_TSDBCLIENT_H
#define _FNORD_TSDB_TSDBCLIENT_H
#include <fnord/stdtypes.h>
#include <fnord/random.h>
#include <fnord/option.h>
#include <fnord/SHA1.h>
#include <fnord/http/httpconnectionpool.h>
#include <tsdb/PartitionInfo.pb.h>
#include <tsdb/RecordEnvelope.pb.h>

using namespace fnord;

namespace tsdb {

class TSDBClient {
public:
  size_t kMaxInsertBachSize = 1024;

  TSDBClient(
      const String& uri,
      http::HTTPConnectionPool* http);

  void insertRecord(const RecordEnvelope& record);
  void insertRecords(const RecordEnvelopeList& records);

  void insertRecord(
      const String& tsdb_namespace,
      const String& stream_key,
      const SHA1Hash& partition_key,
      const SHA1Hash& record_id,
      const Buffer& record_data);

  Vector<String> listPartitions(
      const String& stream_key,
      const UnixTime& from,
      const UnixTime& until);

  void fetchPartition(
      const String& tsdb_namespace,
      const String& stream_key,
      const SHA1Hash& partition_key,
      Function<void (const Buffer& record)> fn);

  void fetchPartitionWithSampling(
      const String& tsdb_namespace,
      const String& stream_key,
      const SHA1Hash& partition_key,
      size_t sample_modulo,
      size_t sample_index,
      Function<void (const Buffer& record)> fn);

  PartitionInfo fetchPartitionInfo(
      const String& tsdb_namespace,
      const String& stream_key,
      const SHA1Hash& partition_key);

  Buffer fetchDerivedDataset(
      const String& stream_key,
      const String& partition,
      const String& derived_dataset_name);

  uint64_t mkMessageID();

protected:

  void insertRecordsToHost(
      const String& host,
      const RecordEnvelopeList& records);

  String uri_;
  http::HTTPConnectionPool* http_;
  Random rnd_;
};

} // namespace tdsb

#endif
