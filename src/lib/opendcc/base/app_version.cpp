// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/app_version.h"

OPENDCC_NAMESPACE_OPEN
namespace base::app_version
{
    const char* get_build_date_str()
    {
        static const char* build_date = __DATE__;
        return build_date;
    }

    const char* get_git_commit_hash_str()
    {
        static const char* git_commit_hash = GIT_COMMIT;
        return git_commit_hash;
    }

}

OPENDCC_NAMESPACE_CLOSE
