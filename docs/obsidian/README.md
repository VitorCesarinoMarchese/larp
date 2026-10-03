# LARP study notes

Start with [the study map](LARP%20-%20Study%20map.md) for the reading order,
milestone status, and acceptance results. These are the LARP study notes
exported from the personal Obsidian vault. Other vault content and Obsidian
settings are excluded.

Links use relative Markdown paths so the notes work in this checkout and on
GitHub. To use them in Obsidian, open the repository as a vault or copy this
folder into an existing vault. Source links require the surrounding repository.

To refresh the export from your own vault, run from the repository root:

```sh
python3 docs/obsidian/sync_from_vault.py /path/to/vault/LARP
```

The script reads only `LARP - *.md` notes. It leaves the original vault
unchanged, converts wiki links to Markdown, and replaces checkout-specific
source links with relative paths. Review and commit the generated notes after
each refresh. Implementation and acceptance details remain in
[the main documentation](../index.md).
