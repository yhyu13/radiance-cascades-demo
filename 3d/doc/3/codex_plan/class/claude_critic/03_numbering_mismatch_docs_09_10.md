# Finding 3 — Low: Title Numbering Mismatch in Docs 09 and 10

**Documents:** `09_current_code_map.md`, `10_phase5d_trilinear_upper_lookup.md`
**Severity:** Low
**Status:** Unfixed

---

## What is wrong

Both documents have a one-off mismatch between their filename number and their title
header number:

| Filename | Title header |
|---|---|
| `09_current_code_map.md` | `# 08 Current Code Map` |
| `10_phase5d_trilinear_upper_lookup.md` | `# 09 Phase 5d: What the Full 3D Upper-Cascade Lookup Would Be` |

The README reading list numbers these as 10 and 11 respectively, which adds a third
inconsistency:

```
10. `09_current_code_map.md`
11. `10_phase5d_trilinear_upper_lookup.md`
```

So the three systems (README list position, filename prefix, title header) all disagree.

---

## Root cause

Most likely the code map and trilinear docs were added after the initial 8-doc series
without updating their title headers to match the new filenames.

---

## Recommended fix

Update the title headers to match the filenames:

- `09_current_code_map.md`: change `# 08 Current Code Map` → `# 09 Current Code Map`
- `10_phase5d_trilinear_upper_lookup.md`: change `# 09 Phase 5d...` → `# 10 Phase 5d...`

Also update the README reading list to use positions 10 and 11 explicitly if numbered
entries are shown (or switch to a bulleted list that removes the ambiguity).
