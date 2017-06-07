#!/usr/bin/env bash

set -e

if ! type sqldiff >/dev/null 2>&1; then
  echo >&2 "Please install sqldiff before running this"
  exit 1
fi

readonly INVOKE_NAME=$0

usage() {
  cat >&2 <<$EOF
Usage: ${INVOKE_NAME} -b dvbindex -r reference_db [options]
This program runs the specified dvbindex binary, telling it to analyze all
streams found in the specified directory, and then compare the obtained database
with the specified reference. If no directory is specified, then the working
directory is processed.
Exit status is 0 if the database matches the reference, otherwise the
differences are printed and 1 is returned.

Additional options :
   -d stream_dir    Analyze all streams found in stream_dir instead of the
                    working directory.
   -k               Keep the created database. It is deleted by default.
   -v               Run dvbindex via Valgrind.
$EOF
  exit 1
}

while getopts 'b:d:kr:v' arg; do
  case "$arg" in
    b) readonly DVBINDEX=$(readlink -f "$OPTARG") ;;
    d) readonly TEST_DIR=$OPTARG ;;
    k) readonly KEEP_DB=1 ;;
    r) readonly REF_DB=$OPTARG ;;
    v) readonly USE_VALGRIND=1 ;;
    *) usage ;;
  esac
done

[[ ! -v DVBINDEX || ! -v REF_DB ]] && usage
[[ ! -v TEST_DIR ]] && readonly TEST_DIR=$PWD
[[ ! -x $DVBINDEX ]] && (echo >&2 "$DVBINDEX is not executable"; exit 1;)
[[ ! -r $REF_DB ]] && (echo >&2 "$REF_DB is not readable"; exit 1;)
[[ ! -d $TEST_DIR ]] && (echo >&2 "$TEST_DIR is not a directory"; exit 1;)

readonly DB_FILE=$(mktemp)

dbfile_on_exit() {
  if [[ $KEEP_DB ]]; then
    echo >&2 "Results saved to $DB_FILE"
  else
    rm "$DB_FILE"
  fi
}

trap dbfile_on_exit EXIT

run_dvbindex() {
  if [[ $USE_VALGRIND ]]; then
    valgrind "$DVBINDEX" "$@"
  else
    "$DVBINDEX" "$@"
  fi
}

readonly -a TEST_STREAMS=('hotbird-uhd.ts' 'unitymedia.ts' 'mux3.ts'
'astra-uhd.ts' 'ISDB-Tb_capture_VLC.ts')
pushd "$TEST_DIR"
run_dvbindex "$DB_FILE" "${TEST_STREAMS[@]}"
popd

result=$(sqldiff "$REF_DB" "$DB_FILE")
if [[ -z $result ]]; then
  exit 0
else
  echo >&2 "$result"
  exit 1
fi
