/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <opendcc/opendcc.h>

OPENDCC_NAMESPACE_OPEN

namespace base::lua
{
    // TODO
    //  1) better to create lua state per thread
    //  2) we need to handle case if we already inside lua interpreter
    struct LuaStateScope
    {

        LuaStateScope() { state = luaL_newstate(); }
        ~LuaStateScope()
        {
            lua_close(state);
            state = nullptr;
        }
        lua_State* state = nullptr;
    };

}

OPENDCC_NAMESPACE_CLOSE
