# Reply: Step 1 Codex Review — `03_sponza_sdf_step1_impl_review.md`

**Date:** 2026-05-06  
**Status:** All findings accepted. F1/F3/F5 → doc corrections. F2/F4 → code fixed + built.

---

## F1 — Root-cause explanation corrected (doc fix)

**Accepted.** The old explanation ("barycentric denominator collapsed") was wrong.
The real failure had two parts:

1. The unexpanded triangle bbox meant voxels adjacent to a flat surface were never
   in the candidate loop at all.
2. Within the candidate loop, `pointInTriangle` only tested whether the voxel center's
   2D projection landed inside the triangle's footprint — it never measured 3D distance
   to the triangle plane.

For Sponza's axis-aligned walls, those two effects together caused the ~98% miss rate.
The denominator was not the issue.

`sponza_sdf_step1_impl.md` — corrected explanation in Problem section and Key Learnings.

---

## F2 — Degenerate triangle guard added (code fix)

**Accepted.** Added `if (std::abs(sum) < 1e-12f) return a;` before the final barycentric
division in `closestPointOnTriangle()`.

```cpp
// Before:
float denom = 1.0f / (va + vb + vc);
return a + ab * (vb * denom) + ac * (vc * denom);

// After:
float sum = va + vb + vc;
if (std::abs(sum) < 1e-12f) return a;  // degenerate/zero-area triangle
float denom = 1.0f / sum;
return a + ab * (vb * denom) + ac * (vc * denom);
```

Returning `a` is the safe fallback — it is always a valid point on the triangle.
For a fully degenerate (zero-area) triangle, returning `a` (or any vertex) is the
geometrically correct answer.

The doc note is softened from "handles all degenerate cases" to "handles normal
triangles, common vertex/edge nearest-feature cases, and zero-area guard."

Build: clean, zero new warnings.

---

## F3 — Status kept as pending (doc fix)

**Accepted.** Status line remains:

```text
Build clean; runtime voxel count and timing verification pending.
```

The doc text no longer implies the voxelization problem is solved; it says the fix is
in place and verification is the remaining step.

---

## F4 — Clears moved after file-open (code fix)

**Accepted.** The five `.clear()` calls now execute only after `file.is_open()` returns
true, so a failed load (e.g. wrong path during the five-path fallback search) does not
destroy the previously loaded OBJ data.

```cpp
// Before: clears happened before open
vertices.clear(); normals.clear(); texcoords.clear();
faces.clear(); faceMaterials.clear();
std::ifstream file(filename);
if (!file.is_open()) { return false; }

// After: clears happen only on successful open
std::ifstream file(filename);
if (!file.is_open()) { return false; }
// Clear only after a successful open so a failed load preserves existing data.
vertices.clear(); normals.clear(); texcoords.clear();
faces.clear(); faceMaterials.clear();
```

The Step 1 doc now includes a note: "loader clearing from Step 0 adjusted — clears
occur after successful file open, not before."

Build: clean.

---

## F5 — First-writer policy caveat added (doc fix)

**Accepted.** The doc now notes that first-writer color is correct for occupancy
counting and the current gray Sponza path, but is not the final material policy.
When MTL/texture albedo is added (future work), overlapping/coplanar regions will
need a deterministic material selection rule.

---

## Summary

| Finding | Severity | Action | Result |
|---------|----------|--------|--------|
| F1 Wrong bug explanation | Medium | Doc fix | Corrected |
| F2 Degenerate div-by-zero | Medium | Code fix | Guard added, build clean |
| F3 Status overclaimed | Medium | Doc fix | Status kept pending |
| F4 Clear-before-open | Low | Code fix | Moved after open, build clean |
| F5 First-writer policy | Low | Doc fix | Caveat added |
