# Performance & Correctness Checklist: Performance & Correctness Optimizations

**Purpose**: Validate that performance optimization and correctness requirements are complete, clear, consistent, and measurable before implementation.
**Created**: 2026-04-06
**Feature**: [spec.md](../spec.md)

## Requirement Completeness

- [ ] CHK001 Are ForeColor/BackColor normalization requirements defined for ALL CopyBits call sites, including transparent-mode sprite blits? [Completeness, Spec §FR-001]
- [ ] CHK002 Are requirements specified for which CopyBits transfer modes (srcCopy vs transparent) need color normalization? [Completeness, Gap]
- [ ] CHK003 Is the deferred rebuild flag lifecycle fully specified — when it is set, checked, and cleared, including screen transitions? [Completeness, Spec §FR-002]
- [ ] CHK004 Are requirements defined for clearing the deferred rebuild flag when transitioning away from the game screen? [Completeness, Edge Case §1]
- [ ] CHK005 Is the tilemap cache population timing explicitly specified (after ScanSpawns, before any game logic)? [Completeness, Spec §FR-005]
- [ ] CHK006 Are requirements defined for what TileMap_Reset does if called before TileMap_Init? [Completeness, Edge Case §3]

## Requirement Clarity

- [ ] CHK007 Is "potential 2.5x speedup" qualified as a theoretical maximum from book benchmarks, not a guaranteed outcome? [Clarity, Spec §US1]
- [ ] CHK008 Are the specific renderer.c functions requiring ForeColor/BackColor changes enumerated in the spec (not just the contracts)? [Clarity, Spec §FR-001]
- [ ] CHK009 Is "exactly once per frame" for background rebuild clearly defined as once per Renderer_BeginFrame call? [Clarity, Spec §US2 Acceptance §1]
- [ ] CHK010 Is the spatial bomb grid data type and size explicitly specified (unsigned char vs int, MAX_GRID_ROWS x MAX_GRID_COLS)? [Clarity, Spec §FR-007]

## Requirement Consistency

- [ ] CHK011 Are the "direct rebuild" vs "deferred rebuild" call site lists consistent between spec §FR-003/§FR-004 and contracts/renderer-api.md? [Consistency]
- [ ] CHK012 Is the TileMap_Reset caller list consistent between spec §FR-006, contracts/tilemap-api.md, and tasks.md T016? [Consistency]
- [ ] CHK013 Are memory budget figures consistent between data-model.md (~1,579 bytes) and plan.md (~1.6 KB)? [Consistency]
- [ ] CHK014 Do the bomb.c modification requirements in US2 (deferred rebuild) and US4 (spatial grid) avoid conflicting changes to the same functions? [Consistency]

## Acceptance Criteria Quality

- [ ] CHK015 Can SC-001 (FPS equal or improved on color Macs) be objectively measured with the existing FPS counter? [Measurability, Spec §SC-001]
- [ ] CHK016 Can SC-002 (background rebuilt exactly once per frame) be verified via existing clog infrastructure? [Measurability, Spec §SC-002]
- [ ] CHK017 Is SC-005 (Mac SE >= 15fps) a valid baseline given that 15fps was only recently measured and may vary? [Measurability, Spec §SC-005]
- [ ] CHK018 Can SC-006 (identical gameplay behavior) be objectively verified, or is it too broad to be measurable? [Measurability, Spec §SC-006]

## Platform Coverage

- [ ] CHK019 Are requirements defined for verifying that ForeColor/BackColor changes don't affect Mac SE 1-bit rendering behavior? [Coverage, Spec §US1 Acceptance §2]
- [ ] CHK020 Are requirements specified for all three build targets in each user story, not just the final polish phase? [Coverage, Spec §FR-010]
- [ ] CHK021 Are requirements defined for how the spatial bomb grid interacts with dynamic grid dimensions (TMAP resource loads)? [Coverage, Gap]

## Edge Case Coverage

- [ ] CHK022 Are requirements defined for bomb placed and removed in the same frame with the spatial grid? [Edge Case §4, Spec §FR-007]
- [ ] CHK023 Are requirements defined for chain explosions where one bomb's explosion detonates another bomb's grid cell? [Edge Case, Gap]
- [ ] CHK024 Are requirements specified for what happens if RebuildBackground is called directly (not deferred) during gameplay accidentally? [Edge Case, Spec §FR-004]
- [ ] CHK025 Are requirements defined for TileMap_Reset when the tilemap has not been modified (no blocks destroyed)? [Edge Case, Spec §FR-005]

## Dependencies & Assumptions

- [ ] CHK026 Is the assumption that transparent-mode CopyBits is unaffected by ForeColor/BackColor validated against the book references? [Assumption, Research §R1]
- [ ] CHK027 Is the assumption that spawn positions never change between rounds explicitly documented? [Assumption, Spec §FR-005]
- [ ] CHK028 Is the dependency between US1 and US2 (both modify renderer.c) documented in the task execution order? [Dependency, Tasks §Dependencies]
- [ ] CHK029 Is the assumption that peer pointers are never dereferenced (only compared) validated against all net.c code paths? [Assumption, Spec §US5]

## Notes

- Check items off as completed: `[x]`
- Items referencing `[Gap]` indicate missing requirements that should be added to the spec
- Items referencing `[Assumption]` should be validated against source code or book references before implementation
- 29 items total across 7 categories
