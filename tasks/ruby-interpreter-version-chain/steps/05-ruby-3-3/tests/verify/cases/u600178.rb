# -*- coding: us-ascii -*-
# frozen_string_literal: false
Klass = Data.define(:foo, :bar)
klass = Data.define

    test = klass.new

    p(test)
    p(test)

    p(test.inspect)
    p(test.members)
    p(test.to_h)