from pathlib import Path
from tempfile import TemporaryDirectory
import json
import unittest

from nspa.fine_grained_reachability import (
    load_validated_memory_functions,
    patch_saber_checker_api,
    run_saber_on_bitcode,
)


SABER_CPP = """
static const ei_pair ei_pairs[]=
{
    {"malloc", SaberCheckerAPI::CK_ALLOC},

    {"free", SaberCheckerAPI::CK_FREE},

    {"fopen", SaberCheckerAPI::CK_FOPEN},

    /* NSPA_AUTO_CURL_EI_PAIRS_BEGIN */
    {"old_alloc", SaberCheckerAPI::CK_ALLOC},
    {"old_free", SaberCheckerAPI::CK_FREE},
    /* NSPA_AUTO_CURL_EI_PAIRS_END */

    {0, SaberCheckerAPI::CK_DUMMY}
};
"""


class FineGrainedReachabilityTests(unittest.TestCase):
    def test_load_validated_memory_functions_filters_macros_and_maps_categories(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "validated.json"
            path.write_text(
                json.dumps(
                    {
                        "functions": [
                            {
                                "name": "curl_alloc",
                                "category": "allocator",
                                "confidence": 0.91,
                                "file": "lib/a.c",
                                "signature": "void *curl_alloc(void)",
                                "cfr": {"entity_kind": "function_definition"},
                            },
                            {
                                "name": "curl_destroy",
                                "category": "destroyer",
                                "confidence": 0.8,
                                "file": "lib/b.c",
                                "signature": "void curl_destroy(void *p)",
                                "cfr": {"entity_kind": "function_definition"},
                            },
                            {
                                "name": "CURLX_MALLOC",
                                "category": "allocator",
                                "confidence": 0.99,
                                "file": "include/curl.h",
                                "signature": "#define CURLX_MALLOC(x)",
                                "cfr": {"entity_kind": "function_like_macro"},
                            },
                        ]
                    }
                ),
                encoding="utf-8",
            )

            funcs = load_validated_memory_functions(path, min_confidence=0.5)

            self.assertEqual([fn.name for fn in funcs], ["curl_alloc", "curl_destroy"])
            self.assertEqual(funcs[0].checker_type, "CK_ALLOC")
            self.assertEqual(funcs[1].checker_type, "CK_FREE")

    def test_patch_saber_checker_api_inserts_blocks_inside_type_groups(self) -> None:
        with TemporaryDirectory() as tmp:
            cpp = Path(tmp) / "SaberCheckerAPI.cpp"
            json_path = Path(tmp) / "validated.json"
            cpp.write_text(SABER_CPP, encoding="utf-8")
            json_path.write_text(
                json.dumps(
                    {
                        "functions": [
                            {
                                "name": "curl_alloc",
                                "category": "allocator",
                                "confidence": 0.9,
                                "file": "lib/a.c",
                                "signature": "void *curl_alloc(void)",
                                "cfr": {"entity_kind": "function_definition"},
                            },
                            {
                                "name": "curl_free",
                                "category": "releaser",
                                "confidence": 0.9,
                                "file": "lib/b.c",
                                "signature": "void curl_free(void *p)",
                                "cfr": {"entity_kind": "function_definition"},
                            },
                        ]
                    }
                ),
                encoding="utf-8",
            )

            funcs = load_validated_memory_functions(json_path)
            alloc_count, free_count = patch_saber_checker_api(cpp, funcs, project_tag="curl")
            text = cpp.read_text(encoding="utf-8")

            self.assertEqual((alloc_count, free_count), (1, 1))
            self.assertNotIn("NSPA_AUTO_CURL_EI_PAIRS_BEGIN", text)
            self.assertLess(text.index("curl_alloc"), text.index('"free"'))
            self.assertLess(text.index("curl_free"), text.index('"fopen"'))
            self.assertIn("NSPA_AUTO_CURL_CK_ALLOC_BEGIN", text)
            self.assertIn("NSPA_AUTO_CURL_CK_FREE_BEGIN", text)

    def test_saber_outputs_are_sparse_by_default(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            saber = root / "fake_saber.py"
            bc = root / "ok.bc"
            out = root / "out"
            bc.write_text("bitcode", encoding="utf-8")
            saber.write_text(
                "#!/usr/bin/env python3\n"
                "print('normal stdout')\n",
                encoding="utf-8",
            )
            saber.chmod(0o755)

            results = run_saber_on_bitcode(
                saber=saber,
                extapi=root / "extapi.bc",
                bc_files=[bc],
                checkers=["leak"],
                output_dir=out,
                progress=False,
            )

            self.assertEqual(results[0].returncode, 0)
            self.assertIsNone(results[0].stdout_file)
            self.assertIsNone(results[0].stderr_file)
            self.assertEqual(list(out.glob("*.stdout.txt")), [])
            self.assertEqual(list(out.glob("*.stderr.txt")), [])

    def test_saber_error_stderr_is_saved_and_stdout_is_optional(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            saber = root / "fake_saber.py"
            bc = root / "bad.bc"
            out = root / "out"
            bc.write_text("bitcode", encoding="utf-8")
            saber.write_text(
                "#!/usr/bin/env python3\n"
                "import sys\n"
                "print('debug stdout')\n"
                "print('error stderr', file=sys.stderr)\n"
                "raise SystemExit(7)\n",
                encoding="utf-8",
            )
            saber.chmod(0o755)

            results = run_saber_on_bitcode(
                saber=saber,
                extapi=root / "extapi.bc",
                bc_files=[bc],
                checkers=["dfree"],
                output_dir=out,
                save_stdout=True,
                progress=False,
            )

            self.assertEqual(results[0].returncode, 7)
            self.assertIsNotNone(results[0].stdout_file)
            self.assertIsNotNone(results[0].stderr_file)
            self.assertIn("debug stdout", Path(results[0].stdout_file).read_text(encoding="utf-8"))
            self.assertIn("error stderr", Path(results[0].stderr_file).read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
