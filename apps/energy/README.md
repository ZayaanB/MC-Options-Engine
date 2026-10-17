# Energy forecasting

Status: planned. There is no energy importer, model runner or measured energy
accuracy yet. Do not pass hourly load or electricity prices to the daily-stock
CLI; its data contract is different.

The first candidate is Ontario hourly electricity **demand**, predicting the
next 24 hours. This is not electricity-price prediction. Demand is a useful
LightGBM hypothesis because calendar patterns and prior load provide features
to test; it is not established as our best-performing task.
A published [load/peak forecasting study](https://arxiv.org/abs/2305.05575)
uses LightGBM, but its results do not establish accuracy for our future model.

## Data and first build

Start with [IESO's demand reports](https://www.ieso.ca/Power-Data/Data-Directory).
Keep downloaded source files, retrieval dates, checksums, units and timezone
rules in `Notes-dont-commit/energy/`. Check data terms and reporting revisions.

1. Build a dedicated importer with unambiguous hourly timestamps and MW units.
   Handle missing/duplicate hours, daylight saving and report availability.
2. Freeze chronological training, validation and final periods. Start with
   previous-day and previous-week baselines; only use values available at origin.
3. Add a CPU LightGBM challenger using calendar and available lagged load.
   Forecast all 24 hours without using realized values from inside that horizon.
4. Report hourly MAE/RMSE, daily peak error and runtime before adding complexity.

Weather can be added only with archived forecasts available at each origin;
future observed weather would leak. Demand reporting delays also need an
explicit availability rule. Electricity prices need a separate target and
evaluation, including spikes and possibly negative values.

## How to run

No run command exists yet. The next implementation belongs in this folder
with its own importer, CLI, tests and optional dependencies. Reuse utilities
only when their contracts fit; keep C++ option pricing independent. See the
[research notes](../../docs/forecast-model-research.md) for candidates.
