# -*- coding: us-ascii -*-
# frozen_string_literal: true
m0 = Module.new do
      def foo
        super
      end
    end

    c1 = Class.new do
      def foo
        :C1
      end
    end

    c2 = Class.new do
      def foo
        :C2
      end
    end

    d1 = Class.new(c1) do
      include m0
    end

    d2 = Class.new(c2) do
      include m0
    end

    p(d1.new.foo)

    m = Module.new do
      def foo
        super
      end
    end

    d1.class_eval do
      include m
    end

    d2.class_eval do
      include m
    end

    p(d2.new.foo)