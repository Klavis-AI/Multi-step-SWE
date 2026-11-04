"""A Python-2 print statement: unparsable on every interpreter in the ladder.

"it does not compile" must never be read as "it contains none of the
constructs", so the scan reports this file rather than skipping it. The two
constructs below would each be a finding if the file parsed; the expectation
records that neither is counted, and that ``unparsable`` is.
"""

import numpy as np

np.random.seed(3)
print "python 2"
