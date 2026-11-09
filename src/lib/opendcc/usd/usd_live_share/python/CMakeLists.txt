set(_install_options)
if(NOT DCC_PACKAGE_OPENDCC_USD_EDITOR_LIVE_SHARE)
    list(APPEND _install_options NO_INSTALL)
endif()

opendcc_make_python_module(
    py_usd_live_share
    NO_BOOST_PYTHON
    ${_install_options}
    PYMODULE_NAME
    usd_live_share
    PYMODULE_SOURCES
    wrap_live_share_session.cpp
    PUBLIC_DEFINITIONS
    BOOST_ALL_DYN_LINK
    PRIVATE_DEFINITIONS
    MODULE_NAME=usd_live_share
    LIBRARY_DEPENDENCIES
    usd_live_share
    PYMODULE_INSTALL_PATH
    packages/opendcc.usd_editor.live_share/python/opendcc/usd_editor/live_share)
