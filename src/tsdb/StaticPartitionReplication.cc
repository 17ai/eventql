/**
 * This file is part of the "tsdb" project
 *   Copyright (c) 2015 Paul Asmuth, FnordCorp B.V.
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <tsdb/StaticPartitionReplication.h>
#include <tsdb/ReplicationScheme.h>
#include <tsdb/TSDBClient.h>
#include <stx/logging.h>
#include <stx/io/fileutil.h>
#include <stx/protobuf/msg.h>

using namespace stx;

namespace tsdb {

StaticPartitionReplication::StaticPartitionReplication(
    RefPtr<Partition> partition,
    RefPtr<ReplicationScheme> repl_scheme,
    http::HTTPConnectionPool* http) :
    PartitionReplication(partition, repl_scheme, http) {}

bool StaticPartitionReplication::needsReplication() const {
  auto replicas = repl_scheme_->replicasFor(snap_->key);
  if (replicas.size() == 0) {
    return false;
  }

  auto repl_state = fetchReplicationState();
  auto head_version = snap_->state.cstable_version();
  for (const auto& r : replicas) {
    const auto& replica_version = replicatedVersionFor(repl_state, r.unique_id);
    if (replica_version < head_version) {
      return true;
    }
  }

  return false;
}

void StaticPartitionReplication::replicateTo(
    const ReplicaRef& replica,
    uint64_t head_version) {
  auto tsdb_url = StringUtil::format("http://$0/tsdb", replica.addr);
  tsdb::TSDBClient tsdb_client(tsdb_url, http_);

  auto pinfo = tsdb_client.partitionInfo(
      snap_->state.tsdb_namespace(),
      snap_->state.table_key(),
      snap_->key);

  if (!pinfo.isEmpty() && pinfo.get().cstable_version() >= head_version) {
    return;
  }

  auto reader = partition_->getReader();
  auto cstable_file = reader->cstableFilename();

  if (cstable_file.isEmpty()) {
    return;
  }

  auto uri = URI(
      StringUtil::format(
          "$0/update_cstable?namespace=$1&table=$2&partition=$3&version=$4",
          tsdb_url,
          URI::urlEncode(snap_->state.tsdb_namespace()),
          URI::urlEncode(snap_->state.table_key()),
          snap_->key.toString(),
          head_version));

  http::HTTPRequest req(http::HTTPMessage::M_POST, uri.pathAndQuery());
  req.addHeader("Host", uri.hostAndPort());
  req.addBody(FileUtil::read(cstable_file.get())); // FIXME use FileUpload

  auto res = http_->executeRequest(req);
  res.wait();

  const auto& r = res.get();
  if (r.statusCode() != 201) {
    RAISEF(kRuntimeError, "received non-201 response: $0", r.body().toString());
  }
}

bool StaticPartitionReplication::replicate() {
  auto replicas = repl_scheme_->replicasFor(snap_->key);
  if (replicas.size() == 0) {
    return true;
  }

  auto repl_state = fetchReplicationState();
  auto head_version = snap_->state.cstable_version();
  bool dirty = false;
  bool success = true;

  for (const auto& r : replicas) {
    const auto& replica_version = replicatedVersionFor(repl_state, r.unique_id);

    if (replica_version < head_version) {
      logDebug(
          "tsdb",
          "Replicating partition $0/$1/$2 to $3",
          snap_->state.tsdb_namespace(),
          snap_->state.table_key(),
          snap_->key.toString(),
          r.addr);

      try {
        replicateTo(r, head_version);
        setReplicatedVersionFor(&repl_state, r.unique_id, head_version);
        dirty = true;
      } catch (const std::exception& e) {
        success = false;

        stx::logError(
          "tsdb",
          e,
          "Error while replicating partition $0/$1/$2 to $3",
          snap_->state.tsdb_namespace(),
          snap_->state.table_key(),
          snap_->key.toString(),
          r.addr);
      }
    }
  }

  if (dirty) {
    commitReplicationState(repl_state);
  }

  return success;
}

} // namespace tdsb

