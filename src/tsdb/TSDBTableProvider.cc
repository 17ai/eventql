/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <stx/SHA1.h>
#include <tsdb/TSDBTableProvider.h>
#include <tsdb/TSDBService.h>
#include <chartsql/CSTableScan.h>
#include <chartsql/runtime/EmptyTable.h>

using namespace stx;

namespace tsdb {

TSDBTableProvider::TSDBTableProvider(
    const String& tsdb_namespace,
    PartitionMap* partition_map,
    CSTableIndex* cstable_index) :
    tsdb_namespace_(tsdb_namespace),
    partition_map_(partition_map),
    cstable_index_(cstable_index) {}

Option<ScopedPtr<csql::TableExpression>> TSDBTableProvider::buildSequentialScan(
      RefPtr<csql::SequentialScanNode> node,
      csql::QueryBuilder* runtime) const {
  auto table_ref = TSDBTableRef::parse(node->tableName());
  if (partition_map_->findTable(tsdb_namespace_, table_ref.table_key).isEmpty()) {
    return None<ScopedPtr<csql::TableExpression>>();
  }

  if (table_ref.host.isEmpty() || table_ref.host.get() != "localhost") {
    RAISEF(
        kRuntimeError,
        "error while opening table '$0': host must be == localhost",
        node->tableName());
  }

  if (table_ref.partition_key.isEmpty()) {
    RAISEF(
        kRuntimeError,
        "error while opening table '$0': missing partition key",
        node->tableName());
  }

  auto cstable = cstable_index_->fetchCSTable(
      tsdb_namespace_,
      table_ref.table_key,
      table_ref.partition_key.get());

  if (cstable.isEmpty()) {
    return Option<ScopedPtr<csql::TableExpression>>(
        mkScoped(new csql::EmptyTable()));
  }

  return Option<ScopedPtr<csql::TableExpression>>(
      mkScoped(
          new csql::CSTableScan(
              node,
              mkScoped(new cstable::CSTableReader(cstable.get())),
              runtime)));
}


void TSDBTableProvider::listTables(
    Function<void (const csql::TableInfo& table)> fn) const {
  partition_map_->listTables(
      tsdb_namespace_,
      [this, fn] (const TSDBTableInfo& table) {
    fn(tableInfoForTable(table));
  });
}

Option<csql::TableInfo> TSDBTableProvider::describe(
    const String& table_name) const {
  auto table_ref = TSDBTableRef::parse(table_name);
  auto table = partition_map_->tableInfo(tsdb_namespace_, table_ref.table_key);
  if (table.isEmpty()) {
    return None<csql::TableInfo>();
  } else {
    return Some(tableInfoForTable(table.get()));
  }
}

csql::TableInfo TSDBTableProvider::tableInfoForTable(
    const TSDBTableInfo& table) const {
  csql::TableInfo ti;
  ti.table_name = table.table_name;

  for (const auto& col : table.schema->columns()) {
    csql::ColumnInfo ci;
    ci.column_name = col.first;
    ci.type = col.second.typeName();
    ci.type_size = col.second.typeSize();
    ci.is_nullable = col.second.optional;

    ti.columns.emplace_back(ci);
  }

  return ti;
}

} // namespace csql
