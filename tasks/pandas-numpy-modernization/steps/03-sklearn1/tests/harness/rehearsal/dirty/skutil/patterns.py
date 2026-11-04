"""One literal carrying an escape Python does not define."""

CURRENCY = "[\$,%]"       # invalid-string-escape

DEFINED = "\n\t\\"        # every escape here is one Python knows
RAW = r"[\$,%]"           # the migrated spelling of CURRENCY
