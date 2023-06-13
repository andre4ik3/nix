#!/usr/bin/env bash

source common.sh

sed -e "s|@localstatedir@|$TEST_ROOT/profile-var|g" -e "s|@coreutils@|$coreutils|g" < ../../scripts/nix-profile.sh.in > "$TEST_ROOT"/nix-profile.sh

USER=foobar $SHELL -e -c ". $TEST_ROOT/nix-profile.sh; set"
USER=foobar $SHELL -e -c ". $TEST_ROOT/nix-profile.sh" # test idempotency
