import csv
from datetime import date, datetime
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from download_stock_history import COLUMNS, download, validate_rows, validate_symbols


START = date(2025, 1, 1)
END = date(2025, 2, 1)


def sample_rows():
    return [{"Date": f"2025-01-{day:02}", "Open": 100.0, "High": 102.0,
             "Low": 99.0, "Close": 101.0, "Adj Close": 100.5, "Volume": 1000.0,
             "Dividends": 0.0, "Stock Splits": 0.0} for day in (2, 3, 6)]


class Frame:
    empty = False
    columns = COLUMNS

    def iterrows(self):
        for row in sample_rows():
            yield datetime.fromisoformat(row["Date"]), row


class DownloadTests(unittest.TestCase):
    def test_snapshot_has_valid_csv_metadata_and_hashes(self):
        calls = []

        def fetch(symbol, **kwargs):
            calls.append((symbol, kwargs))
            return Frame()

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "snapshot"
            summary = download(["AAPL", "SPY"], START, END, output, fetch=fetch, retrieved_on=END)
            self.assertEqual(len(summary["assets"]), 2)
            self.assertEqual(json.loads((output / "download.json").read_text()), summary)
            for asset in summary["assets"]:
                content = (output / asset["csv"]).read_bytes()
                self.assertEqual(hashlib.sha256(content).hexdigest(), asset["csv_sha256"])
                rows = list(csv.DictReader(io.StringIO(content.decode())))
                self.assertEqual(len(rows), 3)
                self.assertEqual(float(rows[0]["Adj Close"]), 100.5)
                metadata = (output / asset["metadata"]).read_bytes()
                self.assertEqual(hashlib.sha256(metadata).hexdigest(), asset["metadata_sha256"])
                self.assertIn(b"frequency=daily", metadata)
                self.assertIn(b"price_column=Adj Close", metadata)
            self.assertFalse(calls[0][1]["auto_adjust"])
            self.assertTrue(calls[0][1]["keepna"])
            self.assertFalse(calls[0][1]["repair"])
            self.assertFalse(calls[0][1]["threads"])

    def test_existing_output_is_never_touched(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            marker = output / "keep.txt"
            marker.write_text("existing user data")
            with self.assertRaisesRegex(ValueError, "already exists"):
                download(["AAPL"], START, END, output, fetch=lambda *a, **k: self.fail("network called"))
            self.assertEqual(marker.read_text(), "existing user data")

    def test_failed_provider_does_not_save_a_partial_snapshot(self):
        calls = []

        def fetch(symbol, **kwargs):
            calls.append(symbol)
            return Frame() if symbol == "AAPL" else None

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "snapshot"
            with self.assertRaisesRegex(ValueError, "no complete OHLCV"):
                download(["AAPL", "SPY", "MSFT"], START, END, output, fetch=fetch, retrieved_on=END)
            self.assertEqual(calls, ["AAPL", "SPY"])
            self.assertFalse(output.exists())

    def test_failed_write_removes_only_the_new_snapshot(self):
        original_open = Path.open

        def fail_metadata(path, *args, **kwargs):
            if path.suffix == ".meta":
                raise OSError("simulated disk failure")
            return original_open(path, *args, **kwargs)

        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "snapshot"
            with patch.object(Path, "open", fail_metadata):
                with self.assertRaisesRegex(OSError, "simulated disk failure"):
                    download(["AAPL"], START, END, output, fetch=lambda *a, **k: Frame(), retrieved_on=END)
            self.assertFalse(output.exists())
            self.assertTrue(Path(temporary).exists())

    def test_unsafe_symbols_and_bad_ranges_fail_before_network(self):
        for symbols in ([], ["../AAPL"], ["AAPL/SPY"], ["aapl"], ["AAPL", "AAPL"]):
            with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                validate_symbols(symbols)
        with self.assertRaises(ValueError):
            download(["AAPL"], END, START, "unused", fetch=lambda *a, **k: self.fail("network called"))
        with self.assertRaises(ValueError):
            download(["AAPL"], START, END, "unused", retrieved_on=START,
                     fetch=lambda *a, **k: self.fail("network called"))

    def test_invalid_provider_data_is_rejected_without_dropping_rows(self):
        mutations = (("Adj Close", float("nan")), ("Close", 0.0), ("High", 98.0),
                     ("Low", 103.0), ("Volume", -1.0), ("Volume", 1.5),
                     ("Dividends", float("inf")), ("Date", "2025-02-01"))
        for column, value in mutations:
            with self.subTest(column=column, value=value), self.assertRaises(ValueError):
                rows = sample_rows()
                rows[0][column] = value
                validate_rows(rows, START, END)
        for rows in (sample_rows()[:2], list(reversed(sample_rows())), [sample_rows()[0]] * 3):
            with self.assertRaises(ValueError):
                validate_rows(rows, START, END)


if __name__ == "__main__":
    unittest.main()
