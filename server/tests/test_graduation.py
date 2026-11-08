from contextlib import closing
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from uuid import UUID, uuid4, uuid5

sys.path.insert(0, str(Path(__file__).parents[1]))
import main


class GraduationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.store = main.Store(root / "collection.sqlite", root / "media")
        self.pc = str(uuid4())
        self.phone = str(uuid4())
        self.deck = {"id": str(uuid4()), "name": "Study", "description": "", "parentId": "",
                     "createdAt": "2026-11-08T00:00:00Z"}
        self.card = {"id": str(uuid4()), "deckId": self.deck["id"], "kind": "basic", "front": "Question",
                     "back": "Answer", "tags": "", "pointCount": 1}
        self.variant = {"id": str(uuid5(UUID(self.card["id"]), "forward")), "cardId": self.card["id"],
                        "key": "forward", "due": "2026-11-08T00:00:00Z", "reviewCount": 3,
                        "stability": 1.0 / 3, "difficulty": .5, "graduated": False}
        self.sync(self.pc, [self.event(self.pc, "deck.upsert", {"deck": self.deck}),
                            self.event(self.pc, "card.upsert", {"card": self.card, "variants": [self.variant]})])

    def tearDown(self):
        self.temporary.cleanup()

    def event(self, device, kind, payload):
        return {"id": str(uuid4()), "deviceId": device, "type": kind,
                "createdAt": "2026-11-08T00:00:00Z", "payload": payload}

    def sync(self, device, events=(), cursor=0):
        return self.store.sync({"deviceId": device, "cursor": cursor, "events": list(events)})

    def review(self, device, graduated, count):
        due = "2026-11-09T00:00:00Z" if graduated else "2026-11-08T00:10:00Z"
        variant = {**self.variant, "reviewCount": count, "stability": 1 if graduated else 1.0 / 144,
                   "graduated": graduated, "due": due}
        review = {"id": str(uuid4()), "cardId": self.card["id"], "variantId": variant["id"],
                  "deckName": "Study", "grade": 3 if graduated else 0,
                  "recallFraction": 1 if graduated else 0, "responseSeconds": 2,
                  "reviewedAt": "2026-11-08T00:00:00Z", "due": due}
        return self.event(device, "review.add", {"review": review, "variant": variant})

    def test_conflicting_learning_review_gets_authoritative_daily_repair(self):
        initial = self.sync(self.pc)["cursor"]
        graduation = self.review(self.pc, True, 4)
        stale = self.review(self.phone, False, 6)
        self.sync(self.pc, [graduation], initial)
        response = self.sync(self.phone, [stale], initial)
        events = [entry["event"] for entry in response["events"]]
        self.assertEqual([event["type"] for event in events], ["review.add", "review.add", "variant.upsert"])
        repair = events[-1]
        self.assertTrue(repair["payload"]["variant"]["graduated"])
        self.assertEqual(repair["payload"]["variant"]["reviewCount"], 6)
        self.assertEqual(response["contentConflictCount"], 1)
        self.assertEqual(self.sync(self.phone, [stale], initial)["events"], response["events"])
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM events WHERE event_id=?", (repair["id"],)).fetchone()[0], 1)

    def test_graduation_after_more_stale_reviews_preserves_count_and_daily_schedule(self):
        initial = self.sync(self.pc)["cursor"]
        stale = self.review(self.phone, False, 6)
        graduation = self.review(self.pc, True, 4)
        self.sync(self.phone, [stale], initial)
        response = self.sync(self.pc, [graduation], initial)
        repair = response["events"][-1]["event"]
        self.assertEqual(repair["type"], "variant.upsert")
        self.assertTrue(repair["payload"]["variant"]["graduated"])
        self.assertEqual(repair["payload"]["variant"]["reviewCount"], 6)
        self.assertEqual(response["contentConflictCount"], 1)

    def test_notice_is_on_repair_page_and_retry_does_not_duplicate_it(self):
        initial = self.sync(self.pc)["cursor"]
        self.sync(self.pc, [self.review(self.pc, True, 4)], initial)
        stale = self.review(self.phone, False, 6)
        original = self.sync(self.phone, [stale], initial)
        cursor, notices, pages = initial, [], []
        with patch.object(main, "RESPONSE_LIMIT", 1):
            while True:
                page = self.sync(self.phone, cursor=cursor)
                pages.extend(page["events"])
                notices.append(page["contentConflictCount"])
                cursor = page["cursor"]
                if not page["hasMore"]:
                    break
        self.assertEqual([page["event"]["type"] for page in pages], ["review.add", "review.add", "variant.upsert"])
        self.assertEqual(notices, [0, 0, 1])
        self.assertEqual(self.sync(self.phone, [stale], initial)["events"], original["events"])

    def test_reset_and_history_undo_may_clear_graduation(self):
        graduation = self.review(self.pc, True, 4)
        self.sync(self.pc, [graduation])
        reset_variant = {**self.variant, "reviewCount": 0, "stability": 1.0 / 144}
        reset = self.event(self.pc, "variant.upsert", {"variant": reset_variant, "reset": True})
        response = self.sync(self.pc, [reset], self.sync(self.pc)["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["variant.upsert"])
        next_review = self.review(self.pc, False, 1)
        response = self.sync(self.pc, [next_review], response["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["review.add"])

        graduation = self.review(self.pc, True, 4)
        self.sync(self.pc, [graduation])
        reverted = {**self.variant, "due": "2026-11-08T00:00:00Z"}
        undo = self.event(self.pc, "history.apply", {"changes": [{"entity": "variant", "id": self.variant["id"],
                            "before": graduation["payload"]["variant"], "after": reverted}]})
        response = self.sync(self.pc, [undo], self.sync(self.pc)["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["history.apply"])
        response = self.sync(self.pc, [self.review(self.pc, False, 4)], response["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["review.add"])

    def test_stale_postponement_does_not_clear_graduation(self):
        initial = self.sync(self.pc)["cursor"]
        self.sync(self.pc, [self.review(self.pc, True, 4)], initial)
        stale = self.event(self.phone, "variant.upsert", {"variant": {**self.variant, "reviewCount": 3,
                           "due": "2026-11-12T00:00:00Z"}})
        response = self.sync(self.phone, [stale], initial)
        repair = response["events"][-1]["event"]
        self.assertEqual(repair["type"], "variant.upsert")
        self.assertTrue(repair["payload"]["variant"]["graduated"])
        self.assertEqual(response["contentConflictCount"], 1)

    def test_unreviewed_postponement_does_not_clear_concurrent_graduation(self):
        initial = self.sync(self.pc)["cursor"]
        self.sync(self.pc, [self.review(self.pc, True, 4)], initial)
        unreviewed = {**self.variant, "reviewCount": 0, "stability": 1.0 / 144,
                      "due": "2026-11-12T00:00:00Z"}
        postpone = self.event(self.phone, "variant.upsert", {"variant": unreviewed})
        response = self.sync(self.phone, [postpone], initial)
        self.assertEqual([row["event"]["type"] for row in response["events"]],
                         ["review.add", "variant.upsert", "variant.upsert"])
        self.assertTrue(response["events"][-1]["event"]["payload"]["variant"]["graduated"])
        self.assertEqual(response["contentConflictCount"], 1)

    def test_orphan_variant_before_legacy_card_creation_does_not_graduate_it(self):
        card = {**self.card, "id": str(uuid4())}
        fresh = {**self.variant, "id": str(uuid5(UUID(card["id"]), "forward")),
                 "cardId": card["id"], "reviewCount": 0, "stability": 1.0 / 144}
        orphan = {**fresh, "reviewCount": 1, "stability": 1, "graduated": True}
        self.sync(self.pc, [self.event(self.pc, "variant.upsert", {"variant": orphan})])
        self.sync(self.pc, [self.event(self.pc, "card.upsert", {"card": card, "variants": [fresh]})])
        review = self.review(self.pc, False, 1)
        review["payload"]["variant"] = {**fresh, "reviewCount": 1}
        review["payload"]["review"].update(cardId=card["id"], variantId=fresh["id"])
        review["payload"]["review"]["due"] = fresh["due"]
        response = self.sync(self.pc, [review], self.sync(self.pc)["cursor"])
        self.assertEqual([row["event"]["type"] for row in response["events"]], ["review.add"])
        self.assertEqual(response["contentConflictCount"], 0)
        with closing(self.store.connect()) as database:
            database.execute("DELETE FROM projection_versions WHERE name='graduation-v2-reset-intent'")
            database.commit()
        self.store = main.Store(Path(self.temporary.name) / "collection.sqlite", Path(self.temporary.name) / "media")
        self.assertEqual(self.sync(self.pc)["cursor"], response["cursor"])

    def test_orphan_removed_direction_cannot_poison_legacy_recreation(self):
        reverse = {**self.variant, "id": str(uuid5(UUID(self.card["id"]), "reverse")), "key": "reverse",
                   "reviewCount": 0, "stability": 1.0 / 144}
        both = {**self.card, "kind": "reverse"}
        self.sync(self.pc, [self.event(self.pc, "card.upsert", {"card": both, "variants": [self.variant, reverse]}),
                            self.event(self.pc, "card.upsert", {"card": self.card, "variants": [self.variant]}),
                            self.event(self.pc, "variant.upsert", {"variant": {**reverse, "reviewCount": 1,
                                                                              "stability": 1, "graduated": True}}),
                            self.event(self.pc, "card.upsert", {"card": both, "variants": [self.variant, reverse]})])
        review = self.review(self.pc, False, 1)
        review["payload"]["variant"] = {**reverse, "reviewCount": 1}
        review["payload"]["review"].update(variantId=reverse["id"], due=reverse["due"])
        response = self.sync(self.pc, [review], self.sync(self.pc)["cursor"])
        self.assertEqual([row["event"]["type"] for row in response["events"]], ["review.add"])
        with closing(self.store.connect()) as database:
            database.execute("DELETE FROM projection_versions WHERE name='graduation-v2-reset-intent'")
            database.commit()
        self.store = main.Store(Path(self.temporary.name) / "collection.sqlite", Path(self.temporary.name) / "media")
        self.assertEqual(self.sync(self.pc)["cursor"], response["cursor"])

    def test_reset_intent_validation_is_atomic(self):
        cursor = self.sync(self.pc)["cursor"]
        fresh = {**self.variant, "reviewCount": 0, "stability": 1.0 / 144}
        for invalid in ("true", 1, None):
            with self.subTest(reset=invalid):
                event = self.event(self.pc, "variant.upsert", {"variant": fresh, "reset": invalid})
                with self.assertRaises(main.ProtocolError) as failure:
                    self.sync(self.pc, [event], cursor)
                self.assertEqual(failure.exception.code, "INVALID_EVENT")
                self.assertEqual(self.sync(self.pc)["cursor"], cursor)
        invalid_count = self.event(self.pc, "variant.upsert", {"variant": self.variant, "reset": True})
        with self.assertRaises(main.ProtocolError) as failure:
            self.sync(self.pc, [invalid_count], cursor)
        self.assertIn("reviewCount=0", failure.exception.message)
        self.assertEqual(self.sync(self.pc)["cursor"], cursor)
        wrong_kind = self.event(self.pc, "card.upsert", {"card": self.card, "variants": [self.variant], "reset": False})
        with self.assertRaises(main.ProtocolError):
            self.sync(self.pc, [wrong_kind], cursor)
        self.assertEqual(self.sync(self.pc)["cursor"], cursor)

    def test_legacy_unmarked_zero_count_is_preserved_conservatively(self):
        initial = self.sync(self.pc)["cursor"]
        graduation = self.review(self.pc, True, 4)
        self.sync(self.pc, [graduation], initial)
        ambiguous = self.event(self.phone, "variant.upsert", {"variant": {**self.variant, "reviewCount": 0,
                               "stability": 1.0 / 144}})
        response = self.sync(self.phone, [ambiguous], initial)
        self.assertEqual([row["event"]["type"] for row in response["events"]],
                         ["review.add", "variant.upsert", "variant.upsert"])
        self.assertTrue(response["events"][-1]["event"]["payload"]["variant"]["graduated"])
        explicit = self.event(self.pc, "variant.upsert", {"variant": {**self.variant, "reviewCount": 0,
                              "stability": 1.0 / 144}, "reset": True})
        response = self.sync(self.pc, [explicit], response["cursor"])
        self.assertEqual([row["event"]["type"] for row in response["events"]], ["variant.upsert"])
        with closing(self.store.connect()) as database:
            self.assertFalse(json.loads(database.execute("SELECT variant_json FROM graduation_heads WHERE variant_id=?",
                                                       (self.variant["id"],)).fetchone()[0])["graduated"])

    def test_valid_grade_correction_may_undo_graduation(self):
        graduation = self.review(self.pc, True, 4)
        first = self.sync(self.pc, [graduation])
        previous_review = graduation["payload"]["review"]
        previous_variant = graduation["payload"]["variant"]
        due = "2026-11-08T08:00:00Z"
        corrected_review = {**previous_review, "grade": 2, "due": due}
        corrected_variant = {**self.variant, "reviewCount": 4, "due": due}
        correction = self.event(self.pc, "review.correct", {"review": corrected_review, "variant": corrected_variant,
                                "previousReview": previous_review, "previousVariant": previous_variant})
        response = self.sync(self.pc, [correction], first["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["review.correct"])
        response = self.sync(self.pc, [self.review(self.pc, False, 5)], response["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["review.add"])

    def test_stale_correction_cannot_clear_newer_review_graduation(self):
        first = self.review(self.pc, True, 4)
        second = self.review(self.pc, True, 5)
        self.sync(self.pc, [first, second])
        previous_review, previous_variant = first["payload"]["review"], first["payload"]["variant"]
        due = "2026-11-08T08:00:00Z"
        correction = self.event(self.pc, "review.correct", {
            "review": {**previous_review, "grade": 2, "due": due},
            "variant": {**self.variant, "reviewCount": 4, "due": due},
            "previousReview": previous_review, "previousVariant": previous_variant})
        cursor = self.sync(self.pc)["cursor"]
        response = self.sync(self.pc, [correction], cursor)
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["review.correct"])
        response = self.sync(self.phone, [self.review(self.phone, False, 6)], response["cursor"])
        self.assertEqual(response["events"][-1]["event"]["type"], "variant.upsert")
        self.assertTrue(response["events"][-1]["event"]["payload"]["variant"]["graduated"])

    def test_creation_context_preserves_phase_and_fresh_variant_clears_it(self):
        initial = self.sync(self.pc)["cursor"]
        self.sync(self.pc, [self.review(self.pc, True, 4)], initial)
        edit = self.event(self.phone, "card.upsert", {"card": {**self.card, "front": "Edited"},
                                                    "variants": [self.variant]})
        self.sync(self.phone, [edit], initial)
        response = self.sync(self.phone, [self.review(self.phone, False, 4)], self.sync(self.pc)["cursor"])
        self.assertTrue(response["events"][-1]["event"]["payload"]["variant"]["graduated"])
        fresh = self.event(self.phone, "card.upsert", {"card": self.card, "variants": [self.variant],
                                                       "freshVariantIds": [self.variant["id"]]})
        response = self.sync(self.phone, [fresh], response["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["card.upsert"])
        response = self.sync(self.phone, [self.review(self.phone, False, 4)], response["cursor"])
        self.assertEqual([entry["event"]["type"] for entry in response["events"]], ["review.add"])

    def test_source_and_generated_repair_roll_back_together(self):
        initial = self.sync(self.pc)["cursor"]
        graduation = self.review(self.pc, True, 4)
        stale = self.review(self.pc, False, 6)
        conflicting = copy.deepcopy(graduation)
        conflicting["payload"]["review"]["responseSeconds"] = 3
        with self.assertRaises(main.Conflict):
            self.sync(self.pc, [graduation, stale, conflicting], initial)
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT MAX(seq) FROM events").fetchone()[0], initial)
            self.assertEqual(database.execute("SELECT COUNT(*) FROM graduation_resolutions").fetchone()[0], 0)
            self.assertEqual(database.execute("SELECT COUNT(*) FROM content_conflicts").fetchone()[0], 0)
        response = self.sync(self.pc, [graduation, stale], initial)
        self.assertEqual([entry["event"]["type"] for entry in response["events"]],
                         ["review.add", "review.add", "variant.upsert"])

    def append_old_log(self, events):
        with closing(self.store.connect()) as database:
            database.execute("BEGIN IMMEDIATE")
            for event in events:
                canonical = main.validate_event(event, event["deviceId"])
                database.execute("INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                                 (event["id"], event["deviceId"], canonical, event["createdAt"]))
            database.execute("DELETE FROM projection_versions WHERE name='graduation-v2-reset-intent'")
            database.commit()

    def test_projection_rebuild_repairs_final_conflict_once_without_rewriting_sources(self):
        graduation, stale = self.review(self.pc, True, 4), self.review(self.phone, False, 6)
        self.append_old_log([graduation, stale])
        self.store = main.Store(self.store.path, self.store.media)
        first = self.sync(self.pc)
        event_types = [row["event"]["type"] for row in first["events"]]
        self.assertEqual(event_types[-3:], ["review.add", "review.add", "variant.upsert"])
        self.assertEqual(first["events"][-3]["event"], graduation)
        self.assertEqual(first["events"][-2]["event"], stale)
        self.assertTrue(first["events"][-1]["event"]["payload"]["variant"]["graduated"])
        self.assertEqual(first["contentConflictCount"], 1)
        self.store = main.Store(self.store.path, self.store.media)
        self.assertEqual(self.sync(self.pc)["events"], first["events"])
        with closing(self.store.connect()) as database:
            database.execute("DELETE FROM projection_versions WHERE name='graduation-v2-reset-intent'")
        self.store = main.Store(self.store.path, self.store.media)
        self.assertEqual(self.sync(self.pc)["events"], first["events"])

    def test_projection_rebuild_never_appends_old_conflict_after_later_reset(self):
        graduation, stale = self.review(self.pc, True, 4), self.review(self.phone, False, 6)
        reset = self.event(self.pc, "variant.upsert", {"variant": {**self.variant, "reviewCount": 0,
                          "stability": 1.0 / 144}, "reset": True})
        self.append_old_log([graduation, stale, reset])
        self.store = main.Store(self.store.path, self.store.media)
        response = self.sync(self.pc)
        self.assertEqual([row["event"] for row in response["events"][-3:]], [graduation, stale, reset])
        self.assertEqual(response["contentConflictCount"], 0)
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM graduation_resolutions").fetchone()[0], 0)

    def test_historical_legacy_reset_is_a_replay_barrier(self):
        mature = self.review(self.pc, True, 30)
        mature["payload"]["variant"].update(stability=30)
        del mature["payload"]["variant"]["graduated"]
        old_reset = {**self.variant, "reviewCount": 0, "stability": 1.0 / 144}
        del old_reset["graduated"]
        reset = self.event(self.pc, "variant.upsert", {"variant": old_reset})
        reviewed = self.review(self.pc, False, 1)
        reviewed["payload"]["variant"].update(stability=.25)
        del reviewed["payload"]["variant"]["graduated"]
        self.append_old_log([mature, reset, reviewed])
        with closing(self.store.connect()) as database:
            original = list(database.execute("SELECT event_json FROM events ORDER BY seq"))
        for _ in range(2):
            self.store = main.Store(self.store.path, self.store.media)
            with closing(self.store.connect()) as database:
                self.assertEqual(list(database.execute("SELECT event_json FROM events ORDER BY seq")), original)
                head = json.loads(database.execute("SELECT variant_json FROM graduation_heads WHERE variant_id=?",
                                                   (self.variant["id"],)).fetchone()[0])
                self.assertEqual(head["stability"], .25)
                database.execute("DELETE FROM projection_versions WHERE name='graduation-v2-reset-intent'")
                database.commit()

    def test_new_legacy_unmarked_zero_count_still_preserves_graduation_after_rebuild(self):
        self.sync(self.pc, [self.review(self.pc, True, 4)])
        old_postpone = {**self.variant, "reviewCount": 0, "stability": 1.0 / 144}
        del old_postpone["graduated"]
        response = self.sync(self.phone, [self.event(self.phone, "variant.upsert", {"variant": old_postpone})],
                             self.sync(self.pc)["cursor"])
        self.assertEqual([row["event"]["type"] for row in response["events"]], ["variant.upsert", "variant.upsert"])
        self.assertEqual(response["contentConflictCount"], 1)
        with closing(self.store.connect()) as database:
            database.execute("DELETE FROM projection_versions WHERE name='graduation-v2-reset-intent'")
            database.commit()
        self.store = main.Store(self.store.path, self.store.media)
        self.assertEqual(self.sync(self.pc)["cursor"], response["cursor"])
        with closing(self.store.connect()) as database:
            head = json.loads(database.execute("SELECT variant_json FROM graduation_heads WHERE variant_id=?",
                                               (self.variant["id"],)).fetchone()[0])
            self.assertTrue(head["graduated"])

    def test_projection_rebuild_keeps_repair_after_correction_that_stays_graduated(self):
        graduation, stale = self.review(self.pc, True, 4), self.review(self.phone, False, 4)
        previous_review, previous_variant = graduation["payload"]["review"], graduation["payload"]["variant"]
        correction = self.event(self.pc, "review.correct", {
            "review": {**previous_review, "grade": 4},
            "variant": {**previous_variant, "difficulty": .3},
            "previousReview": previous_review, "previousVariant": previous_variant})
        self.append_old_log([graduation, stale, correction])
        self.store = main.Store(self.store.path, self.store.media)
        response = self.sync(self.pc)
        self.assertEqual([row["event"]["type"] for row in response["events"][-4:]],
                         ["review.add", "review.add", "review.correct", "variant.upsert"])
        repaired = response["events"][-1]["event"]["payload"]["variant"]
        self.assertTrue(repaired["graduated"])
        self.assertEqual(repaired["difficulty"], .3)
        self.assertEqual(response["contentConflictCount"], 1)

    def test_projection_rebuild_card_deletion_and_restore_clear_old_conflict(self):
        graduation, stale = self.review(self.pc, True, 4), self.review(self.phone, False, 6)
        delete = self.event(self.pc, "card.delete", {"id": self.card["id"]})
        restore = self.event(self.pc, "card.upsert", {"card": self.card, "variants": [self.variant],
                                                     "restore": True, "freshVariantIds": []})
        self.append_old_log([graduation, stale, delete, restore])
        self.store = main.Store(self.store.path, self.store.media)
        response = self.sync(self.pc)
        self.assertEqual([row["event"] for row in response["events"][-4:]], [graduation, stale, delete, restore])
        self.assertEqual(response["contentConflictCount"], 0)

    def test_rebuild_rejected_move_then_restore_does_not_keep_old_phase(self):
        parent = {**self.deck, "id": str(uuid4())}
        self.sync(self.pc, [self.event(self.pc, "deck.upsert", {"deck": parent})])
        graduation = self.review(self.pc, True, 4)
        delete = self.event(self.pc, "deck.delete", {"id": parent["id"]})
        move = self.event(self.phone, "card.upsert", {"card": {**self.card, "deckId": parent["id"]},
                                                     "variants": [self.variant]})
        restore = self.event(self.pc, "card.upsert", {"card": self.card, "variants": [self.variant],
                                                     "restore": True, "freshVariantIds": []})
        stale = self.review(self.phone, False, 4)
        self.append_old_log([graduation, delete, move, restore, stale])
        with closing(self.store.connect()) as database:
            seq = database.execute("SELECT seq FROM events WHERE event_id=?", (delete["id"],)).fetchone()[0]
            scope = {parent["id"]: {"decks": {parent["id"]: ""}, "cards": {}}}
            database.execute("INSERT INTO deletion_scopes VALUES(?,?)", (seq, json.dumps(scope)))
        self.store = main.Store(self.store.path, self.store.media)
        response = self.sync(self.pc)
        self.assertEqual([row["event"] for row in response["events"][-5:]], [graduation, delete, move, restore, stale])
        self.assertEqual(response["contentConflictCount"], 0)


if __name__ == "__main__":
    unittest.main()
