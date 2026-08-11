# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import sys, os


def parse(usdgenschema_path, schema_path, output_dir):
    import importlib.util
    import importlib.machinery

    usdGenSchema_loader = importlib.machinery.SourceFileLoader("usdGenSchema", usdgenschema_path)

    usdGenSchema_spec = importlib.util.spec_from_loader(
        usdGenSchema_loader.name, usdGenSchema_loader
    )
    usdGenSchema = importlib.util.module_from_spec(usdGenSchema_spec)
    sys.modules["usdGenSchema"] = usdGenSchema
    usdGenSchema_spec.loader.exec_module(usdGenSchema)

    usdGenSchema.InitializeResolver()

    # ParseUsd keeps gaining trailing return values between USD versions, so index
    # the fields we need instead of unpacking the whole tuple. Of those, only the
    # position of classes moved: 20.08 added a skip-codegen flag in front of it.
    parsed = usdGenSchema.ParseUsd(schema_path)
    lib_name = parsed[0]
    lib_prefix = parsed[2]
    use_export_api = parsed[4]
    lib_tokens = parsed[5]
    if usdGenSchema.Usd.GetVersion() > (0, 20, 8):
        classes = parsed[7]
    else:
        classes = parsed[6]

    headers = []
    src = []
    src_py = []

    # do not use os.path.join here because on Windows it generates
    # backslashed paths which CMake poorly understands
    def add_header(filename):
        headers.append(output_dir + "/" + filename)

    def add_src(filename):
        src.append(output_dir + "/" + filename)

    def add_src_py(filename):
        src_py.append(output_dir + "/" + filename)

    def add_class(cls):
        add_header(cls.GetHeaderFile())
        add_src(cls.GetCppFile())
        add_src_py(cls.GetWrapFile())

    if use_export_api:
        add_header("api.h")

    token_data = usdGenSchema.GatherTokens(classes, lib_name, lib_tokens)
    if token_data:
        add_header("tokens.h")
        add_src("tokens.cpp")
        add_src_py("wrapTokens.cpp")

    [add_class(cls) for cls in classes]
    return (lib_name, lib_prefix, headers, src, src_py, len(classes) > 0)


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Not enough arguments. Expected path to a schema file and an output directory.")
        sys.exit(-1)

    cmd = sys.executable + " " + " ".join(sys.argv)
    if not os.path.exists(sys.argv[1]):
        print("Failed to parse USD schema. usdGenSchema not found.", cmd)
        sys.exit(-1)

    if not os.path.exists(sys.argv[2]):
        print("Failed to parse USD schema. Expected path to a schema file.\n", cmd)
        sys.exit(-1)

    library_name, library_prefix, gen_headers, gen_src, gen_src_py, have_classes = parse(
        sys.argv[1], sys.argv[2], sys.argv[3]
    )
    print(library_name)
    print(library_prefix)
    print(",".join([h for h in gen_headers]))
    print(",".join([s for s in gen_src]))
    print(",".join([s for s in gen_src_py]))
    print("TRUE" if have_classes else "FALSE")
