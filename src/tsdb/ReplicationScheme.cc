/**
 * This file is part of the "libfnord" project
 *   Copyright (c) 2015 Paul Asmuth
 *
 * FnordMetric is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License v3.0. You should have received a
 * copy of the GNU General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#include <tsdb/ReplicationScheme.h>
#include <stx/fnv.h>

using namespace stx;

namespace tsdb {

Vector<ReplicaRef> StandaloneReplicationScheme::replicasFor(
    const SHA1Hash& key) {
  return Vector<ReplicaRef>{};
}

FixedReplicationScheme::FixedReplicationScheme(
    Vector<ReplicaRef> replicas) :
    replicas_(replicas) {}

Vector<ReplicaRef> FixedReplicationScheme::replicasFor(const SHA1Hash& key) {
  return replicas_;
}

} // namespace tsdb
