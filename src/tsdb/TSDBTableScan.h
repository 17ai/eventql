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
#include <fnord/stdtypes.h>
#include <fnord/protobuf/MessageSchema.h>
#include <dproc/Task.h>
#include <tsdb/TSDBTableScanSpec.pb.h>
#include <tsdb/TSDBTableScanlet.h>
#include <tsdb/TSDBClient.h>

using namespace fnord;

namespace tsdb {

template <typename ScanletType>
class TSDBTableScan : public dproc::RDD {
public:
  typedef typename ScanletType::ResultType ResultType;

  static ResultType mergeResults(
      dproc::TaskContext* context,
      RefPtr<ScanletType> scanlet);

  TSDBTableScan(
      const Buffer& params,
      RefPtr<ScanletType> scanlet,
      msg::MessageSchemaRepository* repo,
      TSDBClient* tsdb);

  TSDBTableScan(
      const TSDBTableScanSpec& params,
      RefPtr<ScanletType> scanlet,
      msg::MessageSchemaRepository* repo,
      TSDBClient* tsdb);

  void compute(dproc::TaskContext* context);
  List<dproc::TaskDependency> dependencies() const;

  RefPtr<VFSFile> encode() const override;
  void decode(RefPtr<VFSFile> data) override;

  ResultType* result();

protected:

  void onRow(const Buffer& row);

  void scanWithoutIndex(dproc::TaskContext* context);
  void scanWithCSTableIndex(dproc::TaskContext* context);

  TSDBTableScanSpec params_;
  RefPtr<ScanletType> scanlet_;
  RefPtr<msg::MessageSchema> schema_;
  TSDBClient* tsdb_;
};


} // namespace tsdb

#include "TSDBTableScan_impl.h"
