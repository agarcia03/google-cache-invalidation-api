#!/bin/sh
# Copyright 2012 Google Inc. All Rights Reserved.
# Tool to output Java classes for the protocol buffers used by the invalidation
# client into the generated-protos/ directory.
TOOL=${PROTOC- `which protoc`}
mkdir -p generated-protos/
$TOOL --java_out=generated-protos/ ../proto/* --proto_path=../proto/
