# v28.9.1 — The ladder learns to scroll

*One line, owner request: the 26-rung SIZE dropdown opened as a
full-height popup covering most of the panel.*

- `setMaxVisibleItems(6)` on the estimate combo: the popup shows six
  rows and scrolls — the sub-hour rungs plus the first hours (where
  most picks live) visible, everything heavier one flick away.
- Pinned in `estimateDropdownSpeaksHours`; caveat filed in code and QA:
  the cap is a style *hint* — fully native popup styles can ignore it,
  so the QA line verifies on real Windows.
- **353 tests green across six suites** (unchanged). Format v13.
