# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import sys, os, subprocess
import argparse
import io

parser = argparse.ArgumentParser(
    description="Internalization generator", formatter_class=argparse.RawTextHelpFormatter
)

subparsers = parser.add_subparsers(title="Subcommands", dest="selected_command")
make_ts = subparsers.add_parser("make_ts", help="Generate *.ts files for the specified lang")
make_ts.add_argument("--src_dir", type=str, help="Root Project directory", required=True)
make_ts.add_argument("--output_dir", type=str, help="*.ts files output directory", required=True)
make_ts.add_argument("--qt_path", type=str, help="Path to qt root directory.", required=True)
make_ts.add_argument("--no-obsolete", action="store_true", help="Drop obsolete messages.")
make_ts.add_argument("--silent", action="store_true", help=" Do not explain what is being done.")
make_ts.add_argument(
    "--lang",
    type=str,
    help="*.ts file language",
    default="all",
    choices=["ru", "en", "all"],
    required=True,
)


make_qm = subparsers.add_parser("make_qm", help="Merge default *.qm files into single *.qm")
make_qm.add_argument("--qt_path", type=str, help="Path to qt root directory.", required=True)
make_qm.add_argument(
    "-i", "--inputs", type=str, nargs="+", help="Input file to merge", required=True
)
make_qm.add_argument("--output_dir", help="*.qm files output directory", required=True)


args = parser.parse_args()


def generate_ts_new(qt_path, src_dir, output_dir, no_obsolete, silent, lang):
    i18n = os.path.join(output_dir, "i18n.{}.ts".format(lang))
    lupdate = os.path.join(qt_path, "bin", "lupdate")

    cmd = [
        lupdate,
        "-extensions",
        "h,hpp,c,cpp,c++,cxx,py",
        src_dir,
        "-tr-function-alias",
        "translate+=i18n",
        "-ts",
        i18n,
    ]
    if no_obsolete:
        cmd.append("-no-obsolete")
    if silent:
        cmd.append("-silent")

    print("Executing lupdate:", " ".join(cmd))
    p = subprocess.Popen(cmd)
    p.communicate()


def generate_qm(qt_path, inputs, output_dir):
    lrelease = os.path.join(qt_path, "bin", "lrelease")
    for i in inputs:
        lang = i.split(".")[-2]  # assumption input is i18n.<lang>.qm

        # QTranslator fails the whole catalogue if a declared dependency is missing, so chain Qt's
        # own translations in only when that .qm exists. A source-built Qt may ship none at all.
        loader_filename = None
        if os.path.exists(os.path.join(qt_path, "translations", "qt_{}.qm".format(lang))):
            loader = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="{0}">
    <dependencies>
        <dependency catalog="qt_{0}"/>
    </dependencies>
</TS>
""".format(
                lang
            )
            loader_filename = os.path.join(output_dir, "loader.{}.ts".format(lang))
            with io.open(loader_filename, "w", encoding="utf-8") as loader_f:
                loader_f.write(loader.decode("utf-8") if sys.version_info[0] < 3 else loader)

        cmd = []
        cmd.append(lrelease)
        cmd.append(i)
        if loader_filename:
            cmd.append(loader_filename)
        cmd.append("-qm")
        cmd.append(
            os.path.join(output_dir, "{}.qm".format(os.path.splitext(os.path.basename(i))[0]))
        )
        print("Executing lrelease:", " ".join(cmd))
        p = subprocess.Popen(cmd)
        p.communicate()
        if loader_filename:
            os.remove(loader_filename)


if __name__ == "__main__":
    if args.selected_command == "make_ts":
        langs = [args.lang] if args.lang != "all" else ["ru", "en"]
        for lang in langs:
            generate_ts_new(
                args.qt_path, args.src_dir, args.output_dir, args.no_obsolete, args.silent, lang
            )
    elif args.selected_command == "make_qm":
        generate_qm(args.qt_path, args.inputs, args.output_dir)
    else:
        parser.print_help()
    sys.exit(0)
