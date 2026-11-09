from pathlib import Path
import sys
import unittest
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).parents[1]))
import main


class ClozeMarkerTests(unittest.TestCase):
    def card(self, front):
        return {
            "id": str(uuid4()),
            "deckId": str(uuid4()),
            "kind": "cloze",
            "front": front,
            "back": "",
            "tags": "",
            "pointCount": 1,
        }

    def test_numbered_markers_share_groups(self):
        card = self.card(r"[cloze1:std::expected|type] and [cloze2:\[literal\] \| value] [cloze1:second]")
        self.assertEqual(main.card_keys(card), ["c1", "c2"])

    def test_numbered_marker_allows_multiline_markdown_answer(self):
        self.assertEqual(main.card_keys(self.card("Use **[cloze999:first\nsecond]**.")), ["c999"])

    def test_literal_curly_braces_and_code_are_answer_content(self):
        self.assertEqual(main.card_keys(self.card("[cloze1:std::array<int,2> a{{1,2}}]")), ["c1"])

    def test_escaped_brackets_are_literal_answer_content(self):
        self.assertEqual(main.card_keys(self.card(r"[cloze1:array\[index\]]")), ["c1"])

    def test_ordinary_and_escaped_brackets_are_not_markers(self):
        self.assertEqual(main.card_keys(self.card(r"[ordinary] and \[cloze1:literal\] [cloze2:real]")), ["c2"])

    def test_malformed_numbered_markers_are_rejected(self):
        for source in (
            "[cloze0:answer]",
            "[cloze1000:answer]",
            "[cloze1:]",
            "[cloze1:answer|]",
            "[cloze1:answer",
            "[cloze1:outer [cloze2:inner]]",
            "[cloze1:array[index]]",
            "[cloze0:x] [cloze1:y]",
            "[cloze1:answer\\]",
            "{{c1::old syntax}}",
        ):
            with self.subTest(source=source), self.assertRaises(main.ProtocolError):
                main.card_keys(self.card(source))


if __name__ == "__main__":
    unittest.main()
