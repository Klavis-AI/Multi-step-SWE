# -*- coding: us-ascii -*-
# frozen_string_literal: true
m0 = Module.new do
      def foo; :M0 end
    end
    m1 = Module.new do
      include m0
    end
    c = Class.new do
      include m1
      alias bar foo
    end
    d = Class.new(c) do
    end

    test = -> do
      d.new.bar
    end

    p(test[])

    c.class_eval do
      def bar
        :C
      end
    end

    p(test[])