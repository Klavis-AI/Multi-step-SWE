# -*- coding: us-ascii -*-
# frozen_string_literal: false
FOO = "foo"
key = "a"
    h = {key => 100}
    key.upcase!
    p(h['a'])