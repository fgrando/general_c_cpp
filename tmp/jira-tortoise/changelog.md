# Changelog entries

One row per entry. `ref` is the id that `commits.md` points at through its
`changelog-ref` field: a JIRA key when a task exists, otherwise a synthetic
`X-nnnn` that concentrates changes with no ticket behind them.

`jira` holds one or more keys, comma-separated, or `none` when the change was
never ticketed. A literal `|` inside `text` must be escaped as `\|`.

## Types

`type` must be exactly one of the following. The set is closed.

| type | meaning |
|---|---|
| `new` | functionality that did not exist before |
| `changed` | existing behaviour altered, not a defect correction |
| `bugfix` | a defect corrected |
| `deprecation` | still present and working, but scheduled for removal |
| `removed` | gone as of this release |
| `security` | vulnerability closed |
| `ci` | build or pipeline change with no effect on the delivered software |
| `known-issue` | open defect shipped knowingly, carried in the release note |

Two that are easy to get wrong:

- `ci` is about what the change does **not** affect. A pipeline change that
  alters the delivered binary — a compiler flag, a link order — is `changed`,
  not `ci`, however build-shaped the diff looks.
- `known-issue` rows have no commits behind them. That is the point of them,
  and any orphan check has to allow it.

## Entries

| ref | jira | type | text |
|---|---|---|---|
| CL5-1 | AVX-1187 | bugfix | The event log no longer discards the oldest record buffer size. |
| CL5-3 | AVX-1204 | changed | Log timestamps are now taken from the free-running timer. |
| CL5-2 | AVX-1211 | new | Added a runtime severity threshold. |
| CL5-5 | none | changed | The build now fails immediately when the cross-compiler version does not match. |
| CL5-6 | none | known-issue | ignore this entry. |
