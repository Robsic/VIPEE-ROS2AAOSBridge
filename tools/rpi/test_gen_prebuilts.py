import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_prebuilts as g

# Fake readelf: mapeia nome do arquivo para a lista de NEEDED.
NEEDED = {
    "liba.so": ["libb.so", "libc.so", "liblog.so"],
    "libb.so": ["libc++_shared.so", "libm.so"],
    "libc++_shared.so": ["libc.so", "libdl.so"],
    "vipee_bridge": ["liba.so", "libc++_shared.so", "libc.so", "libdl.so"],
}


def fake_readelf(path):
    return NEEDED[path.rsplit("/", 1)[-1]]


class Gen(unittest.TestCase):
    def libs(self):
        return {n: "/x/lib/" + n for n in ("liba.so", "libb.so", "libc++_shared.so")}

    def gen(self, libs=None, **kw):
        return g.generate(self.libs() if libs is None else libs,
                          "/x/vipee_bridge", "vipee_bridge.rc",
                          needed_of=fake_readelf, **kw)

    def test_one_shared_library_module_per_lib(self):
        bp = self.gen()
        for name in ("liba", "libb", "libc++_shared"):
            self.assertIn(f'name: "{name}"', bp)
        self.assertEqual(bp.count("cc_prebuilt_library_shared {"), 3)
        self.assertEqual(bp.count("vendor: true"), 4)
        self.assertEqual(bp.count("check_elf_files: false"), 3)
        self.assertIn('srcs: ["lib/liba.so"]', bp)

    def test_binary_has_init_rc_and_only_dir_libs_in_shared_libs(self):
        bp = self.gen()
        binary = bp[bp.index("cc_prebuilt_binary {"):]
        self.assertIn('name: "vipee_bridge"', binary)
        self.assertIn('init_rc: ["vipee_bridge.rc"]', binary)
        self.assertIn('"liba"', binary)
        self.assertIn('"libc++_shared"', binary)
        self.assertNotIn('"libc"', binary)
        self.assertNotIn('"libdl"', binary)

    def test_missing_needed_fails_with_named_error(self):
        libs = self.libs()
        del libs["libb.so"]
        with self.assertRaises(g.PrebuiltsError) as cm:
            self.gen(libs)
        self.assertEqual(str(cm.exception), "NEEDED_UNRESOLVED liba.so libb.so")

    def test_binary_needed_missing_names_the_binary(self):
        libs = self.libs()
        del libs["libc++_shared.so"]
        del NEEDED["libb.so"][0]  # só o binário e a lib c++ pedem
        try:
            with self.assertRaises(g.PrebuiltsError) as cm:
                self.gen(libs)
        finally:
            NEEDED["libb.so"].insert(0, "libc++_shared.so")
        self.assertEqual(str(cm.exception),
                         "NEEDED_UNRESOLVED vipee_bridge libc++_shared.so")

    def test_versioned_so_outside_dir_is_unresolved(self):
        NEEDED["libb.so"].append("libfoo.so.1")
        try:
            with self.assertRaises(g.PrebuiltsError) as cm:
                self.gen()
        finally:
            NEEDED["libb.so"].pop()
        self.assertEqual(str(cm.exception), "NEEDED_UNRESOLVED libb.so libfoo.so.1")

    def test_module_prefix_renames_modules_but_keeps_installed_name(self):
        # libyaml.so colide com o módulo libyaml do AOSP: o nome do módulo
        # ganha o prefixo e o arquivo instalado (stem) continua o mesmo.
        bp = self.gen(module_prefix="vipee_ros_")
        self.assertIn('name: "vipee_ros_liba",\n    stem: "liba",', bp)
        self.assertNotIn('name: "liba"', bp)
        binary = bp[bp.index("cc_prebuilt_binary {"):]
        self.assertIn('name: "vipee_bridge"', binary)
        self.assertIn('shared_libs: [\n        "vipee_ros_liba",\n'
                      '        "vipee_ros_libc++_shared",\n    ],', binary)

    def test_binary_requires_every_lib(self):
        # rmw e typesupport são abertos por dlopen: instalar o binário tem de
        # instalar todas as libs, não só as NEEDED dele (libb não é).
        binary = self.gen()[self.gen().index("cc_prebuilt_binary {"):]
        self.assertIn('required: [\n        "liba",\n        "libb",\n'
                      '        "libc++_shared",\n    ],', binary)

    def test_all_modules_are_64_bit_only(self):
        self.assertEqual(self.gen().count('compile_multilib: "64"'), 4)

    def test_parse_readelf_output(self):
        out = (" 0x0000000000000001 (NEEDED)  Shared library: [libb.so]\n"
               " 0x000000000000000e (SONAME)  Library soname: [liba.so]\n"
               " 0x0000000000000001 (NEEDED)  Shared library: [libc.so]\n")
        self.assertEqual(g.parse_needed(out), ["libb.so", "libc.so"])


if __name__ == "__main__":
    unittest.main()
