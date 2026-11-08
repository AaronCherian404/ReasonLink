# Full draft review — November 8, 2026

The draft has the outline and subject matter of a computer-architecture paper.
Completing the TODOs would strengthen it substantially, but would not by itself
establish an HPCA-level contribution. The decisive question is whether the work
demonstrates a new architectural mechanism or a general, explanatory finding
beyond existing accelerator-interface and state-reuse techniques. This is a
research judgment, not a prediction of acceptance.

HPCA explicitly includes accelerator architectures, reconfigurable systems,
and evaluation of real systems, and recognizes multiple evaluation methods;
there is no single mandatory experimental recipe.
[Official HPCA evaluation objectives](https://conf.researchr.org/track/hpca-2027/hpca-2027-main-conference/).

## Principal findings, in priority order

### 1. The novelty argument remains incomplete

Conversion and communication between accelerators are established research
topics. [DMX (HPCA 2024)](https://research.ibm.com/publications/data-motion-acceleration-chaining-cross-domain-multi-accelerators)
addresses data movement, restructuring, and interoperability between heterogeneous
accelerators. [AccelFlow (HPCA 2026)](https://2026.hpca-conf.org/details/hpca-2026-main-conference/71/AccelFlow-Orchestrating-an-On-Package-Ensemble-of-Fine-Grained-Accelerators-for-Micr)
addresses accelerator orchestration and execution without per-stage CPU involvement.
These papers do not establish that ReasonLink's solver-state restoration has
already been done, but they rule out treating the broad interface problem alone
as the distinguishing contribution.

The related-work comparison must identify the specific advance in solver-native
updates, restoring learned-state mutations, or predicting interface-design choices.
Compare incremental/assumption-based SAT techniques as well as hardware systems.
State explicitly which mechanisms come from SAT-Accel and which are new here.

### 2. Current comparisons do not isolate hardware placement alone

The CPU path classifies all givens before generating a delta; ReasonLink processes
outputs incrementally and can overlap conversion with subsequent classifications.
The CPU writes a 2,916-byte answer stack plus 1,024 bytes of counters, while the
hardware writes the same stack plus 32 bytes: 3,940 versus 2,948 bytes. Thus both
schedule and payload differ alongside computation placement.

These are valid implemented-system comparisons, but an experiment attributing a
gain specifically to hardware generation needs matched payloads and an incremental
CPU baseline, or separate ablations quantifying those differences. Descriptor
submission and credit waits are currently charged to the hardware path's inference
interval. Define common timing boundaries before claiming a conversion-only gain.

Evidence: `../../board/rl_board.c` (`prepare` and `prepare_rl`) and
`../../sataccel_hw/drv/sataccel_drv.c` (`sa_dev_load_decisions`).

### 3. Generality requires a defined interface, not just more application names

The RTL currently selects among Sudoku digits and encodes `9 * cell + digit`.
The resident solver path also assumes repeated use of the same base formula.
An autonomous-driving workload must specify the actual perception outputs,
symbolic task, constraint representation, correctness criteria, and changes to
this mapping. Replaying Sudoku-shaped tensors under another application name
would not establish support for that application.

Useful breadth varies symbolic-state size, update size, mutation behavior, producer
rate, and reasoning difficulty. These properties explain where the mechanisms
apply. A supported class of interfaces is a stronger claim than arbitrary
neural-to-symbolic interoperability.

Evidence: `../../sataccel_hw/rtl/reasonlink_core.v` and the resident-base design.

### 4. The evaluation needs a result beyond the expected bottleneck tradeoff

With interface cost fixed, its fractional benefit decreases as solver time grows;
this is already predicted by the additive timing model. Testing H3 is useful,
but confirming that trend alone is unlikely to be a distinctive architectural
finding. Measure where CPU-generated updates, hardware-generated updates, and
on-chip base reuse become worthwhile, and explain the boundaries using control
cost, state size, update rate, and memory behavior.

The present Linux NVDLA path dominates a roughly 2.5-second pipeline. Profile
device execution separately from runtime/driver overhead. Controlled producer-rate
experiments can explore other regimes, but label replay/model results separately
from physical accelerator measurements. Hardware need not beat software in every
regime for the study to be valuable.

### 5. Clean measurement and meaningful baselines remain essential

The adjusted wall times are diagnostic estimates that subtract above-median neural
time. Replace them with clean repeated runs for final performance claims; retain
the unadjusted data. Compare hardware against the optimized CPU-generated delta
baseline as well as the full-image paths. A 35% cost in the allocated-capacity
baseline alone would not show a bottleneck that survives straightforward software
optimization.

Use randomized run order, consistent warm-up/cold-start treatment, timeouts, and
uncertainty across independent runs. Existing runs have zero warm-ups and exclude
initial base-image loading from wall time. The first solve still populates the
on-chip base. State both initialization and steady-state costs explicitly.

### 6. Technical presentation needs more mechanism-level explanation

Add restoration pseudocode and an explicit invariant for base clauses, literal-list
tails, learned-clause allocators, and mutable metadata. Explain invalidation on a
base change. Include a useful boundary/failure example and the resource/control
cost of the mechanism. OOC LUT estimates, HLS estimates, and routed resources
must remain clearly distinguished. Energy measurements are needed if making
energy-efficiency claims; they are not a universal prerequisite for this study.

## What is already working well

- The CPU-generated delta baseline separates representation reuse from the choice
  to implement generation in hardware.
- Functional, RTL, and board checks provide complementary correctness evidence.
- The design distinguishes DRAM base reuse from on-chip base reuse and explains
  the stale learned-state problem.
- Scope and uncertainty are visible, and stage-level improvements are not presented
  as equivalent end-to-end speedups.
- The introduction now follows a problem, observation, mechanism, evaluation, and
  contribution sequence without interrupting the argument to define experiment labels.

## Remaining editorial work after the experiments

The abstract and introduction are more coherent than the first draft, but the
finished paper should replace provisional results and future-tense statements with
the conclusions established by the completed experiments. Add a substantive
related-work section; the existing background currently names infrastructure more
than it positions the contribution. Condense the integration bug history to the
details needed to understand the design and its validation. Present measurement
limitations once in the methodology and refer back to them where needed, rather
than repeatedly defending every comparison. Keep the final discussion focused on
what explains the results and which interface properties determine applicability.

The two current figures explain connectivity and total time, but they do not yet
show a microarchitectural contribution or a range of design regimes. Add the
restoration/control mechanism and the explanatory sensitivity result produced by
the new experiments. Those figures should carry the main argument of the final paper.

## Verification performed for this review

An independent standard-library audit of all 13 raw CSVs reproduced the recorded
whole-run wall times and median neural, conversion, transfer, synchronization,
and solver timings. Every run contains 100 puzzles. SAT/UNSAT outcomes, grids,
decisions, and conflicts agree across configurations. Returned SAT grids satisfy
row, column, and box constraints. This audit does not independently prove UNSAT
or revalidate predictions against images; the paper's functional/reference tests
remain the evidence for those properties.

No new FPGA runs or performance experiments were performed. Figure assets were
regenerated from the current summary, and the PDF was rebuilt and checked for
unresolved references and layout overflow.

## Changes made during the review

- Removed Section VI-C and redistributed every planned task to nearby TODOs; the
  mapping is recorded in the README.
- Added TODOs for novelty positioning, matched CPU/hardware controls, workload
  definitions, restoration pseudocode, and inference-path profiling.
- Documented timing-boundary and initialization details verified against the harness.
- Replaced wording claiming that the current matrix fully isolates mechanisms
  with wording describing the comparisons it actually performs.
- Corrected the conclusion's grammar and removed its submission-plan sentence.
- Preserved the existing empty appendix rather than restoring removed content.

## Assessment if all TODOs are completed

The result could be a credible architecture research paper if the experiments
produce a clear finding and the related-work comparison establishes its novelty.
A broader characterization that explains when state reuse and hardware generation
are worthwhile is one plausible direction. A specific, demonstrably new state-reuse
mechanism is another. If all additional workloads merely reproduce an expected
software-versus-hardware tie, without a new mechanism or explanatory result,
completing the checklist would still leave the main contribution unresolved.
