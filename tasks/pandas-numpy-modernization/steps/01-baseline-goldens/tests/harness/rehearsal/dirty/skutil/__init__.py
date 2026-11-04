"""The synthetic failure fixture.

Every rule in the catalogue has at least one positive occurrence somewhere in
this package, and ``expected.json`` records how many. A rule that fires zero
times here is a rule that has never been shown to fire at all, which is the
failure mode trap 6 of the authoring prompt is about: a gate that always reports
clean reports clean on a submission that did nothing.

Where a rule has more than one arm -- an import form and an attribute form, a
Name and an Attribute -- each arm gets its own occurrence, so the recorded count
is above one on purpose. The arithmetic is written out in ``expected.json``.
"""
