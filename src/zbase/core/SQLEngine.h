/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <csql/runtime/runtime.h>
#include <csql/runtime/ResultFormat.h>
#include <zbase/core/TSDBTableProvider.h>
#include <zbase/AnalyticsAuth.h>

namespace zbase {
class TSDBService;

class SQLEngine {
public:

  static RefPtr<csql::TableProvider> tableProviderForNamespace(
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& tsdb_namespace);

  static RefPtr<csql::QueryTreeNode> rewriteQuery(
      csql::Runtime* runtime,
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& tsdb_namespace,
      RefPtr<csql::QueryTreeNode> query);

  static RefPtr<csql::ExecutionStrategy> getExecutionStrategy(
      csql::Runtime* runtime,
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& customer);

protected:

  // rewrite tbl.lastXXX to tbl WHERE time > x and time < x
  static void rewriteTableTimeSuffix(
      RefPtr<csql::QueryTreeNode> node);

  static void insertPartitionSubqueries(
      csql::Runtime* runtime,
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& tsdb_namespace,
      RefPtr<csql::QueryTreeNode>* node);

  static void replaceSequentialScanWithUnion(
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      const String& tsdb_namespace,
      RefPtr<csql::QueryTreeNode>* node);

  static void shardGroupBy(
      csql::Runtime* runtime,
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& tsdb_namespace,
      RefPtr<csql::QueryTreeNode>* node);

  static ScopedPtr<InputStream> executeParallelGroupBy(
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& customer,
      const Vector<ReplicaRef>& hosts,
      const csql::RemoteAggregateParams& params);

  static ScopedPtr<InputStream> executeRemoteGroupBy(
      PartitionMap* partition_map,
      ReplicationScheme* replication_scheme,
      CompactionWorker* cstable_index,
      AnalyticsAuth* auth,
      const String& customer,
      const InetAddr& host,
      const csql::RemoteAggregateParams& params);

};

}
