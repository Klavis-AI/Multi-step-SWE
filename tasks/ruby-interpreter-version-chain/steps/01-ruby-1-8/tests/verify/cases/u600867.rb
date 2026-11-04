# -*- coding: us-ascii -*-
# frozen_string_literal: false
FOO = "foo"
key = "a".freeze
    h = {key => 100}
    p(h['a'])
    p(*h.keys.equal?(key))