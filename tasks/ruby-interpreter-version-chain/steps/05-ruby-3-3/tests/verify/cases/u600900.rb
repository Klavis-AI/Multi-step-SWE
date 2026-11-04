# frozen_string_literal: true
c0 = Class.new{}
    c1 = Class.new(c0){}
    c0.new.foo rescue nil
    c1.new.foo rescue nil
    c1.module_eval{ def foo = :c1 }
    c0.module_eval{ def foo = :c0 }

    p(c0.new.foo)