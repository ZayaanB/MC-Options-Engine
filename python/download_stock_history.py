import argparse
import csv
from datetime import date
import hashlib
from importlib.metadata import version
import io
import json
import math
from pathlib import Path
import re
import tempfile


COLUMNS = ("Open", "High", "Low", "Close", "Adj Close", "Volume", "Dividends", "Stock Splits")


def validate_symbols(symbols):
    if not symbols or len(set(symbols)) != len(symbols):
        raise ValueError("provide distinct ticker symbols")
    if any(not re.fullmatch(r"[A-Z][A-Z0-9.-]{0,14}", symbol) for symbol in symbols):
        raise ValueError("ticker symbols must be uppercase and contain no path separators")


def validate_rows(rows, start, end):
    if len(rows) < 3:
        raise ValueError("provider returned fewer than three daily observations")
    previous = None
    for row in rows:
        day = date.fromisoformat(row["Date"])
        if row["Date"] != day.isoformat() or not start <= day < end:
            raise ValueError("provider returned a date outside the requested daily range")
        if previous is not None and day <= previous:
            raise ValueError("provider dates must be unique and chronological")
        previous = day
        for column in COLUMNS:
            value = float(row[column])
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"invalid provider value in {column} on {day}")
            if column in COLUMNS[:5] and value == 0:
                raise ValueError(f"nonpositive provider price in {column} on {day}")
        if row["Volume"] != int(row["Volume"]):
            raise ValueError(f"noninteger provider volume on {day}")
        tolerance = 1e-9 * max(row[column] for column in COLUMNS[:4])
        if (row["Low"] > min(row["Open"], row["Close"]) + tolerance or
                row["High"] < max(row["Open"], row["Close"]) - tolerance or
                row["High"] < row["Low"]):
            raise ValueError(f"inconsistent provider OHLC on {day}")


def csv_text(rows):
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=("Date", *COLUMNS), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def download(symbols, start, end, output, fetch=None, retrieved_on=None):
    validate_symbols(symbols)
    retrieved_on = retrieved_on or date.today()
    if start >= end or end > retrieved_on:
        raise ValueError("require start < end <= retrieval date; end is exclusive")
    output = Path(output)
    if output.exists():
        raise ValueError("output directory already exists; use a new snapshot name")
    payloads = {}
    assets = []
    with tempfile.TemporaryDirectory(prefix="mc-stock-cache-") as cache:
        if fetch is None:
            try:
                import yfinance as yf
            except ImportError as error:
                raise ValueError("install python/requirements-data.txt in a virtual environment") from error
            yf.set_tz_cache_location(cache)
            fetch = yf.download
            provider_version = version("yfinance")
        else:
            provider_version = "test-double"
        for symbol in symbols:
            frame = fetch(symbol, start=start.isoformat(), end=end.isoformat(),
                          auto_adjust=False, actions=True, interval="1d", keepna=True,
                          repair=False, multi_level_index=False, threads=False,
                          progress=False, timeout=20)
            if frame is None or frame.empty or any(column not in frame.columns for column in COLUMNS):
                raise ValueError(f"{symbol}: provider returned no complete OHLCV history; stop without retrying")
            rows = [{"Date": timestamp.date().isoformat(),
                     **{column: float(row[column]) for column in COLUMNS}}
                    for timestamp, row in frame.iterrows()]
            validate_rows(rows, start, end)
            csv_content = csv_text(rows)
            source = f"https://finance.yahoo.com/quote/{symbol}/history/"
            metadata = (f"provider=Yahoo Finance via yfinance {provider_version}\n"
                        f"symbol={symbol}\n"
                        "adjustment=Adj Close as reported by Yahoo; split/dividend methodology not independently verified\n"
                        f"retrieved_on={retrieved_on.isoformat()}\nsource_url={source}\n"
                        "price_column=Adj Close\nfrequency=daily\n")
            payloads[f"{symbol}.csv"] = csv_content
            payloads[f"{symbol}.meta"] = metadata
            assets.append({"symbol": symbol, "csv": f"{symbol}.csv", "metadata": f"{symbol}.meta",
                           "csv_sha256": hashlib.sha256(csv_content.encode()).hexdigest(),
                           "metadata_sha256": hashlib.sha256(metadata.encode()).hexdigest(),
                           "rows": len(rows), "first_date": rows[0]["Date"], "last_date": rows[-1]["Date"]})
    summary = {"provider": "Yahoo Finance", "yfinance_version": provider_version,
               "retrieved_on": retrieved_on.isoformat(), "start_inclusive": start.isoformat(),
               "end_exclusive": end.isoformat(), "assets": assets,
               "limitations": ["Provider-reported retrospective adjustments, not point-in-time fundamentals.",
                               "A small present-day ticker list is not a survivorship-free stock universe.",
                               "Session completeness is not verified against an exchange calendar."]}
    payloads["download.json"] = json.dumps(summary, indent=2, allow_nan=False) + "\n"
    output.mkdir(parents=True, exist_ok=False)
    created = []
    try:
        for name, content in payloads.items():
            path = output / name
            with path.open("x", encoding="utf-8", newline="") as destination:
                created.append(path)
                destination.write(content)
    except BaseException:
        for path in reversed(created):
            path.unlink()
        output.rmdir()
        raise
    return summary


def main():
    parser = argparse.ArgumentParser(description="Download validated daily Yahoo OHLCV; never overwrite snapshots.")
    parser.add_argument("--symbols", nargs="+", default=["AAPL", "MSFT", "GOOGL", "SPY", "QQQ", "XLK"])
    parser.add_argument("--start", type=date.fromisoformat, default=date(2010, 1, 1))
    parser.add_argument("--end", type=date.fromisoformat, default=date.today(), help="exclusive; today's session is excluded")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        result = download(arguments.symbols, arguments.start, arguments.end, arguments.output)
    except Exception as error:
        parser.exit(1, f"Download failed: {error}\n")
    for asset in result["assets"]:
        print(f"{asset['symbol']}: {asset['rows']} rows, {asset['first_date']} to {asset['last_date']}")
    print(f"Saved validated snapshot: {arguments.output}")


if __name__ == "__main__":
    main()
