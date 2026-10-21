"""数据生成行为验收；只在内存捕获官方生成器输出，不修改正式生成表。"""
import contextlib
import io
import json
import re
import runpy
import unittest
import tempfile
import copy
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent


def generated_text():
    module = runpy.run_path(str(ROOT / "tools/extract-data.py"))
    output = []
    with patch.object(Path, "write_text", lambda _path, text, **_kwargs: output.append(text)):
        with contextlib.redirect_stdout(io.StringIO()):
            module["main"]()
    return output[0]


def table_rows(text, name):
    body = re.search(rf"{name}_TABLE\(\).*?data = \{{(.*?)\n    \}};", text, re.S).group(1)
    return [json.loads("[" + row.strip()[1:-2] + "]")
            for row in body.splitlines() if row.strip().startswith("{")]


class ReferenceDataTests(unittest.TestCase):
    def test_generated_file_matches_current_bytes(self):
        expected = generated_text().replace("\n", "\r\n").encode("utf-8")
        self.assertEqual((ROOT / "data/reference_tables.h").read_bytes(), expected)

    def test_revision_catalog_and_healing(self):
        rows = table_rows(generated_text(), "RecipeDef")
        self.assertEqual(len(rows), 123)
        self.assertEqual(len({row[5] for row in rows}), 14)
        by_id = {row[0]: row for row in rows}
        for recipe_id, amount in {"5001": 40, "5003": 40, "5101": 200, "5104": 200,
                                  "5201": 800, "5301": 2000, "5401": 2000, "5501": 0}.items():
            self.assertEqual(by_id[recipe_id][6], amount)
        self.assertTrue({"5519", "5318", "5319"}.isdisjoint(by_id))

    def test_survival_values_and_removed_herbs(self):
        text = generated_text(); by_id = {row[0]: row for row in table_rows(text, "RecipeDef")}
        for recipe_id, amount in {"5111": 10, "5212": 20, "5316": 40, "5420": 80,
                                  "5119": 10, "5211": 20, "5308": 40, "5404": 70, "5517": 120}.items():
            self.assertEqual(by_id[recipe_id][6], amount)
        self.assertEqual([by_id[key][6] for key in ["5209", "5306", "5417", "5515"]], [3, 7, 20, 40])
        herbs = table_rows(text, "HerbDef")
        self.assertEqual(len(herbs), 155)
        self.assertTrue({"6323", "6324"}.isdisjoint(row[0] for row in herbs))

    def test_invalid_overrides_fail_before_generation(self):
        module = runpy.run_path(str(ROOT / "tools/extract-data.py"))
        source = {"recipes": [{"id": "5001", "value": 20}]}
        invalid = [
            [{"table": "unknown", "id": "5001", "set": {"value": 40}}],
            [{"table": "recipes", "id": "unknown", "set": {"value": 40}}],
            [{"table": "recipes", "id": "5001", "set": {"unknown": 40}}],
            [{"table": "recipes", "id": "5001", "set": {"value": "40"}}],
            [{"table": "recipes", "id": "5001", "set": {"value": 40}}] * 2,
        ]
        with tempfile.TemporaryDirectory(prefix="ningzhou-data-test-") as directory:
            path = Path(directory) / "overrides.json"
            for changes in invalid:
                with self.subTest(changes=changes):
                    path.write_text(json.dumps({"schemaVersion": 1, "changes": changes}), encoding="utf-8")
                    with self.assertRaises(ValueError):
                        module["apply_overrides"](copy.deepcopy(source), {}, path)

    def test_story_health_cost_matches_absolute_health_rule(self):
        rows = {row[0]: row for row in table_rows(generated_text(), "StoryDef")}
        self.assertEqual(rows["rootpool"][9], "资质+3，损失气血上限12%")


if __name__ == "__main__":
    unittest.main()
