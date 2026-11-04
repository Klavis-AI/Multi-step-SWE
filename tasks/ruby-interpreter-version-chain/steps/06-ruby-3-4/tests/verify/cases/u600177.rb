# -*- coding: us-ascii -*-
# frozen_string_literal: false
Klass = Data.define(:foo, :bar)
klass1 = Data.define(:a)
    klass2 = Data.define(:a)
    o1 = klass1.new(1)
    o2 = klass1.new(1)
    o3 = klass2.new(1)
    p(o1.send(:eql?, o2))
    assert_not_operator(o1, :eql?, o3)