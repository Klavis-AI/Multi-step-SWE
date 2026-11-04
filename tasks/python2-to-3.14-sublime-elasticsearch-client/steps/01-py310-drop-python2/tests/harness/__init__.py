# -*- coding: utf-8 -*-
"""Behaviour scorer for the sublime-elasticsearch-client migration task.

The package is self-contained and never imports the tree under test at module
scope: it is copied verbatim into every step's ``tests/`` directory, where the
tree lives at ``/app`` and is imported by the case functions only.
"""
