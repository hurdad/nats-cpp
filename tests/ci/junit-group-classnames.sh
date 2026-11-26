#!/usr/bin/env bash
# Rewrite CTest's JUnit report so each test's classname is its group: CTest sets classname to the full test
# name, so Codecov showed "integration.foo::integration.foo"; afterwards it shows "integration::foo".
#
#   tests/ci/junit-group-classnames.sh <junit.xml>
#
# Does nothing when the file is missing (e.g. the build failed before ctest ran).
set -euo pipefail

file=${1:?usage: junit-group-classnames.sh <junit.xml>}
[ -f "$file" ] || exit 0
sed -E -i 's/<testcase name="([a-z]+)\.([^"]+)" classname="[^"]*"/<testcase name="\2" classname="\1"/' "$file"
