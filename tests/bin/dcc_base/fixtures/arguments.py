# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import sys

assert sys.argv[1:] == [
    "--gui",
    "--exec",
    "argument with spaces",
    "-platform",
    "opendcc-test-script-argument",
], sys.argv
print("arguments preserved", flush=True)
