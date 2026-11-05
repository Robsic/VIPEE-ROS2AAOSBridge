#!/usr/bin/env python3
"""Gera o ros_prebuilts.bp da imagem do Raspberry Pi (RPI-04).

Lê um diretório de libs (ex.: push_arm64/lib) e o binário vipee_bridge, e
escreve um cc_prebuilt_library_shared por lib e o cc_prebuilt_binary
vipee_bridge. Falha com NEEDED_UNRESOLVED <arquivo> <needed> se alguma
dependência não está no diretório nem na lista SYSTEM_LIBS.

Uso:
  gen_prebuilts.py --lib-dir push_arm64/lib --binary bridge_build_arm64/vipee_bridge \
      --init-rc vipee_bridge.rc [--readelf llvm-readelf] [--out ros_prebuilts.bp]
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

# Libs que o Android (bionic/NDK) fornece em tempo de execução. Qualquer outra
# NEEDED precisa estar no diretório de libs; libc++_shared.so NÃO está aqui de
# propósito: a ponte e as libs ROS usam c++_shared, então ela tem de ser empacotada.
SYSTEM_LIBS = frozenset(
    ["libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libz.so"])

NDK_READELF = ("/opt/android-ndk-r27/toolchains/llvm/prebuilt/linux-x86_64/bin/"
               "llvm-readelf")


class PrebuiltsError(Exception):
    """Erro nomeado; str() é a linha de log (NEEDED_UNRESOLVED <arquivo> <needed>)."""


def parse_needed(readelf_output):
    """Extrai os nomes NEEDED da saída de `llvm-readelf -d`."""
    return re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", readelf_output)


def readelf_needed(readelf):
    """Devolve needed_of(path) que chama `readelf -d path`."""
    def needed_of(path):
        out = subprocess.run([readelf, "-d", path], check=True, text=True,
                             stdout=subprocess.PIPE).stdout
        return parse_needed(out)
    return needed_of


def _mod(so):
    return so[:-3]  # libfoo.so -> libfoo


def generate(libs, binary, init_rc, needed_of, src_prefix="lib/"):
    """libs: {nome.so: caminho}; binary: caminho do vipee_bridge; devolve o texto do .bp."""
    def resolve(owner, path):
        deps = needed_of(path)
        for n in deps:
            if n not in libs and n not in SYSTEM_LIBS:
                raise PrebuiltsError(f"NEEDED_UNRESOLVED {owner} {n}")
        return [n for n in deps if n in libs]

    parts = []
    for name in sorted(libs):
        resolve(name, libs[name])
        parts.append(f'cc_prebuilt_library_shared {{\n    name: "{_mod(name)}",\n'
                     f'    srcs: ["{src_prefix}{name}"],\n    vendor: true,\n'
                     '    check_elf_files: false,\n}\n')
    deps = resolve(os.path.basename(binary), binary)
    shared = "".join(f'        "{_mod(n)}",\n' for n in sorted(deps))
    parts.append('cc_prebuilt_binary {\n    name: "vipee_bridge",\n'
                 '    srcs: ["vipee_bridge"],\n    vendor: true,\n'
                 f'    init_rc: ["{init_rc}"],\n    shared_libs: [\n{shared}    ],\n}}\n')
    return "// Gerado por tools/rpi/gen_prebuilts.py; não editar.\n\n" + "\n".join(parts)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--lib-dir", required=True)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--init-rc", required=True)
    ap.add_argument("--readelf", default=shutil.which("llvm-readelf") or NDK_READELF)
    ap.add_argument("--out", default="-")
    a = ap.parse_args(argv)
    libs = {os.path.basename(p): p for p in glob.glob(os.path.join(a.lib_dir, "*.so"))}
    try:
        bp = generate(libs, a.binary, a.init_rc, readelf_needed(a.readelf))
    except PrebuiltsError as e:
        print(f"ERRO: {e}", file=sys.stderr)
        return 1
    if a.out == "-":
        sys.stdout.write(bp)
    else:
        with open(a.out, "w") as f:
            f.write(bp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
