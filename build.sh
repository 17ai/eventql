#!/bin/bash
set -e

BUILD_ID=$1
if [[ -z "$BUILD_ID" ]]; then
  echo "usage: $0 <BUILD_ID>";
  exit 1
fi

BUILD_TYPE=${BUILD_TYPE:-release}
BUILD_DIR=${BUILD_DIR:-./build}
BUILD_ARCH=${BUILD_ARCH:-$(uname -m)}
BUILD_OS=${BUILD_OS:-$(uname | tr '[:upper:]' '[:lower:]')}
BUILD_NCPUS=${BUILD_NCPUS:-$[ $(getconf _NPROCESSORS_ONLN) / 2 ]}
BUILD_DOCUMENTATION=${BUILD_DOCUMENTATION:-true}
BUILD_ASSETS=${BUILD_ASSETS:-true}
BUILD_ARTIFACTS=${BUILD_ARTIFACTS:-true}
RUN_TESTS=${RUN_TESTS:-true}

if [[ "${BUILD_ARCH}" == "x86" ]]; then
  ARCHFLAGS="-m32 -march=i386";
fi

if [[ "${BUILD_ARCH}" == "x86_64" ]]; then
  ARCHFLAGS="-m64 -march=x86-64";
fi

if [[ -z "$MAKETOOL" ]]; then
  MAKETOOL="make"
  if which ninja > /dev/null; then
    MAKETOOL="ninja"
  fi
fi

echo    "======================  zScale Z1 ======================="
echo -e "                                                         "
echo -e "    Build-ID:      ${BUILD_ID}"
echo -e "    Build-Type:    ${BUILD_TYPE}"
echo -e "    Build-Target:  ${BUILD_OS}/${BUILD_ARCH}"
echo -e "    Build Assets:  ${BUILD_ASSETS}"
echo -e "    Build Docs:    ${BUILD_DOCUMENTATION}"
echo -e "    Maketool:      ${MAKETOOL}"
echo -e "                                                         "
echo    "========================================================="

TARGET_LBL="${BUILD_TYPE}_${BUILD_OS}_${BUILD_ARCH}"
TARGET_DIR="${BUILD_DIR}/${BUILD_ID}/${TARGET_LBL}"
ARTIFACTS_DIR="${BUILD_DIR}/${BUILD_ID}"
mkdir -p $TARGET_DIR
mkdir -p ${ARTIFACTS_DIR}
mkdir -p ${TARGET_DIR}/package
TARGET_DIR_REAL=$(cd ${TARGET_DIR} && pwd)
SOURCE_DIR_REAL=$(pwd)

# build assets
if [[ $BUILD_ASSETS == "true" ]]; then
  (
    cat ./src/zbase/webui/PACKFILE;
    cat ./src/zbase/webdocs/PACKFILE;
    find src/zbase/webui/assets -type f | while read l; do echo ${l/src\//}:$l; done
    find src/zbase/webui/views -type f | while read l; do echo ${l/src\//}:$l; done
    find src/zbase/webui/apps -type f | while read l; do echo ${l/src\//}:$l; done
    find src/zbase/webui/widgets -type f | while read l; do echo ${l/src\//}:$l; done
    find src/zbase/webui/util -type f | while read l; do echo ${l/src\//}:$l; done
    find src/zbase/webui/report-widgets -type f | while read l; do echo ${l/src\//}:$l; done
  ) | ./src/stx/assets.sh ${TARGET_DIR}/zbase_assets.cc
fi

# build documentation
if [[ $BUILD_DOCUMENTATION == "true" ]]; then
  rm -rf $TARGET_DIR/docs
  cp -r doc/ $TARGET_DIR/docs

  (cd $TARGET_DIR && find docs -name "*.md") | while read filename; do
    page_name=$(echo $filename | sed -e 's/^docs\///' -e 's/\.md$//')
    mkdir -p ${TARGET_DIR}/$(dirname docs/${page_name})

    echo "Compiling markdown: docs/${page_name}.md"
    node 3rdparty/markdown-js/convert.js \
        ${TARGET_DIR}/docs/${page_name}.md \
        ${TARGET_DIR}/docs/${page_name}.html
  done

  (cd $TARGET_DIR && find docs -type f) | \
      while read f; do echo zbase/docs/${f/docs\//}:${TARGET_DIR}/$f; done | \
     ./src/stx/assets.sh ${TARGET_DIR}/zbase_documentation_assets.cc
fi

# build c++ with make
if [[ $MAKETOOL == "make" ]]; then
  if [[ ! -e ${TARGET_DIR}/Makefile ]]; then
    CFLAGS="${ARCHFLAGS} ${CFLAGS}" \
    CXXFLAGS="${ARCHFLAGS} ${CXXFLAGS}" \
    ZBASE_BUILD_ID="${BUILD_ID}" cmake \
        -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -B${TARGET_DIR_REAL} \
        -H${SOURCE_DIR_REAL}
  fi

  (cd ${TARGET_DIR} && make -j${BUILD_NCPUS}) || exit 1

# build c++ with ninja
elif [[ $MAKETOOL == "ninja" ]]; then
  if [[ ! -e ${TARGET_DIR}/build.ninja ]]; then
    CFLAGS="${ARCHFLAGS} ${CFLAGS}" \
    CXXFLAGS="${ARCHFLAGS} ${CXXFLAGS}" \
    ZBASE_BUILD_ID="${BUILD_ID}" cmake \
        -G "Ninja" \
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
        -B${TARGET_DIR_REAL} \
        -H${SOURCE_DIR_REAL}
  fi

  (cd ${TARGET_DIR} && ninja -j${BUILD_NCPUS}) || exit 1

else
  echo "error unknown build tool ${MAKETOOL}" >&2
  exit 1
fi

# test
if [[ $RUN_TESTS == "true" ]]; then
  find ${TARGET_DIR} -maxdepth 1 -name "test-*" -type f -exec ./{} \;
fi

# pack artifacts
if [[ $BUILD_ARTIFACTS == "true" ]]; then
  # zbase-core
  tar cz -C ${TARGET_DIR} zbase zbasectl \
      > ${ARTIFACTS_DIR}/zbase-core-${TARGET_LBL}.tgz

  # zbase-master
  tar cz -C ${TARGET_DIR} zmaster \
      > ${ARTIFACTS_DIR}/zbase-master-${TARGET_LBL}.tgz

  # ztracker
  tar cz -C ${TARGET_DIR} ztracker \
      > ${ARTIFACTS_DIR}/ztracker-${TARGET_LBL}.tgz

  # zlogjoin
  tar cz -C ${TARGET_DIR} zlogjoin \
      > ${ARTIFACTS_DIR}/zlogjoin-${TARGET_LBL}.tgz

  # zbroker
  tar cz -C ${TARGET_DIR} brokerd brokerctl \
      > ${ARTIFACTS_DIR}/zbroker-${TARGET_LBL}.tgz

  # zen-utils
  if [[ "${BUILD_TYPE}" == "release" ]]; then
    strip ${TARGET_DIR}/zen-csv-upload
  fi

  tar cz -C ${TARGET_DIR} zen-csv-upload \
      > ${ARTIFACTS_DIR}/zen-csv-upload-${TARGET_LBL}.tgz

  if [[ "${BUILD_TYPE}" == "release" ]]; then
    strip ${TARGET_DIR}/zen-mysql-upload
  fi

  tar cz -C ${TARGET_DIR} zen-mysql-upload \
      > ${ARTIFACTS_DIR}/zen-mysql-upload-${TARGET_LBL}.tgz

  if [[ "${BUILD_TYPE}" == "release" ]]; then
    strip ${TARGET_DIR}/zen-statsd-upload
  fi

  tar cz -C ${TARGET_DIR} zen-statsd-upload \
      > ${ARTIFACTS_DIR}/zen-statsd-upload-${TARGET_LBL}.tgz

  if [[ "${BUILD_TYPE}" == "release" ]]; then
    strip ${TARGET_DIR}/zen-logfile-upload
  fi

  tar cz -C ${TARGET_DIR} zen-logfile-upload \
      > ${ARTIFACTS_DIR}/zen-logfile-upload-${TARGET_LBL}.tgz

  tar cz -C ${TARGET_DIR} zen-csv-upload zen-mysql-upload zen-statsd-upload \
      > ${ARTIFACTS_DIR}/zen-utils-${TARGET_LBL}.tgz
fi
