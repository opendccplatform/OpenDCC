# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import sys, os, subprocess
import argparse
import io
import xml.etree.ElementTree as ET


def make_side_config(typesystem_paths, includes, output_dir, *extra):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    side_config_txt = os.path.join(output_dir, "side_config.txt.in")

    sys.stdout.write("    Executing: {}".format(" ".join(sys.argv)))

    with open(side_config_txt, "w") as f:
        f.write(
            """[generator-project]

generator-set = shiboken
enable-pyside-extensions
enable-parent-ctor-heuristic
enable-return-value-heuristic
avoid-protected-hack
use-isnull-as-nb_nonzero
debug-level = full

{extra}

header-file = @HEADER_FILE_PATH@
typesystem-file = @TYPESYSTEM_FILE_PATH@
language-level = c++@CMAKE_CXX_STANDARD@
{include_paths}
typesystem-path = @PYSIDE_TYPESYSTEMS@
{typesystem_paths}
framework-include-path = @DCC_QT_INSTALL_PREFIX@/lib
""".format(
                extra="\n".join(*extra),
                include_paths="\n".join(["include-path = " + s for s in includes if s]),
                typesystem_paths="\n".join(
                    ["typesystem-path = " + s for s in typesystem_paths if s]
                )
                if typesystem_paths
                else "",
            )
        )


def parse_typesystem(file, output_dir):
    namespace = "opendcc"
    # Load the XML file
    tree = ET.parse(file)
    root = tree.getroot()

    # Extract package prefix
    package_prefix = root.attrib["package"].split(".")[1:]

    # Extract package name
    package_name = root.attrib["package"].split(".")[-1]

    # Find all load-typesystem elements
    load_typesystems = root.findall(".//load-typesystem")

    # Extract load-typesystem names
    load_typesystem_names = [load_type.attrib["name"] for load_type in load_typesystems]

    # Find all object and value type elements
    object_types = root.findall(".//object-type") + root.findall(".//value-type")

    # Extract object-type names
    type_names = [obj_type.attrib["name"] for obj_type in object_types]

    dir = os.path.normpath(os.path.join(output_dir, namespace, *package_prefix)).replace("\\", "/")
    src_rel_dir = os.path.normpath(os.path.join(namespace, *package_prefix)).replace("\\", "/")
    headers = ",".join(
        ["{}/{}_{}_wrapper.h".format(dir, namespace, str.lower(s)) for s in type_names]
    )

    headers += ",{}/{}_{}_python.h".format(dir, namespace, "_".join(package_prefix))

    sources = ",".join(
        ["{}/{}_{}_wrapper.cpp".format(dir, namespace, str.lower(s)) for s in type_names]
    )
    sources += ",{}/{}_module_wrapper.cpp".format(dir, package_name)

    typesystems = ",".join(load_typesystem_names)

    print("{}\n{}\n{}\n{}\n{}".format(package_name, src_rel_dir, headers, sources, typesystems))


def main():
    parser = argparse.ArgumentParser(
        description="Shiboken generator helper", formatter_class=argparse.RawTextHelpFormatter
    )

    subparsers = parser.add_subparsers(title="Subcommands", dest="selected_command")
    make_side_config_parser = subparsers.add_parser(
        "make_side_config", help="Generate side_config.txt.in file."
    )
    make_side_config_parser.add_argument(
        "-t", "--typesystem_paths", type=str, nargs="*", help="Typesystem file path."
    )
    make_side_config_parser.add_argument(
        "-o",
        "--output_dir",
        type=str,
        help="Output dir for generated side_config.txt.in.",
        required=True,
    )
    make_side_config_parser.add_argument(
        "-i", "--include_paths", type=str, nargs="+", help="Include paths."
    )

    parse_typesystem_parser = subparsers.add_parser(
        "parse_typesystem",
        help="Parse typesystem .xml file and get sources that will be generated.",
    )
    parse_typesystem_parser.add_argument(
        "-t", "--typesystem", type=str, help="Typesystem .xml file.", required=True
    )
    parse_typesystem_parser.add_argument(
        "-o", "--output_dir", type=str, help="Output dir for generated source files.", required=True
    )

    args, extra = parser.parse_known_args()

    if args.selected_command == "make_side_config":
        make_side_config(args.typesystem_paths, args.include_paths, args.output_dir, extra)
    elif args.selected_command == "parse_typesystem":
        parse_typesystem(args.typesystem, args.output_dir)


if __name__ == "__main__":
    main()
