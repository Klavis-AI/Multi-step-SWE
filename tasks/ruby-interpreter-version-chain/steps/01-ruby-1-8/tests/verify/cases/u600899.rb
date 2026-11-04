# frozen_string_literal: true
c0 = Class.new{ def foo; end }
    c1 = Class.new(c0){ undef foo }
    c2 = Class.new(c1)
    c3 = Class.new(c2)
    _c4 = Class.new(c3)

    begin; c3.new.foo
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c2.class_eval do
      def foo; :c2; end
    end

    begin; c1.new.foo
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(c3.new.foo)