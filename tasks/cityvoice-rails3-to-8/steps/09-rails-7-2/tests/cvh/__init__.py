"""CityVoice behaviour record/replay harness.

One package serves two roles:

  * inside a separate recorder image it drives the untouched baseline
    (Rails 4.0.0 / Ruby 2.1.7) and writes the goldens;
  * inside every step's verifier image it drives the candidate's tree and
    compares against those goldens.

Both roles run the same scenarios through the same normaliser, so a difference
in the report is a difference in the application's observable behaviour and
not a difference in the instrument.
"""

__all__ = ["norm", "httpc", "sink", "stub", "scenarios", "runner", "compare"]
