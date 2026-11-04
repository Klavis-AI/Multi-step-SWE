"""Steps 5 and 6: the two distutils, which must not be counted as each other."""

from distutils.core import setup                      # stdlib-distutils
from numpy.distutils.misc_util import Configuration   # numpy-distutils

UNUSED = (setup, Configuration)
