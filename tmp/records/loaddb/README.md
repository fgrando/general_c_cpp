# loaddb (schema 2)

One JSON object per load package under `packages/`, named by
`load_package_md5`. Shared fields at top level; `components` is a list of
`{component, crc32}`.

Query with loaddb_query.py:
  --crc32 / --crc-of  identify a dumped component
  --package           list a package's components
  --search TEXT       find packages by free text in notes
