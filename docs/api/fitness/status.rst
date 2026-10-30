fitness/status.hpp
==================

The status component grades a candidate's realisability. ``fitness.status_grading`` picks the scale, and both front ends score on it. They differ only in how a candidate decomposes into components and guarantee-side parts, and in how realisability is queried.

``status_score_mrs`` is the default. It runs ``black`` on each component formula and then on the whole specification, and scores 0.0 if any is unsatisfiable. Otherwise it walks the guarantee side part by part, keeps each part whose addition leaves the kept subset realisable and well-separated, and scores the fraction kept, so 1.0 means every part was kept. ``specification_status`` tests each requirement's condition and response apart, as a screen ahead of the walk rather than a satisfiability test of the specification, and walks one part per guarantee. ``tlsf_status`` uses the individual formulae of the six TLSF sections and walks the parts ``tlsf::split_guarantee_parts`` returns.

A three-point ``"tiered"`` scale scored 1.0 for realisable, 0.5 for every component satisfiable but the specification unrealisable, and 0.0 for an unsatisfiable component. Over the 21 specifications under ``examples/`` it scored every one 0.5, and it was removed on 2026-10-30.

``status_score_aurus`` is not an improvement on the default but a reproduction of AuRUS's own ladder: five levels below realisability, graded by which side of the candidate is satisfiable on its own, so an ablation can cross PEREDUR's grading against the design it derives from. It is selected by ``fitness.status_grading = "aurus"``, it has no per-component tier, and it is the one scorer here that does not fold well-separation into its realisability query. AuRUS never asks that question, and an arm that asked it would measure something else.

Weighted equally with the semantic similarity component so realisable candidates are strongly preferred throughout evolution.

.. doxygenfile:: status.hpp
