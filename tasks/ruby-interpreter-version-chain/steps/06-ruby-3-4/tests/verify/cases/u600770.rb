# frozen_string_literal: false
class C
    def call(*args, **kw)
      yield(self, *args, **kw)
    end
  end
def m_bug20570(*a, **nil)
    a
  end
kw = {}
    h = {:a=>1}
    h2 = {'a'=>1}
    h3 = {'a'=>1, :a=>1}

    c = C.new
    def c.m(*args)
      args
    end
    p(c.call(**{}, &:m))
    p(c.call(**kw, &:m))
    p(c.call(**h, &:m))
    p(c.call(h, **{}, &:m))
    p(c.call(a: 1, &:m))
    p(c.call(**h2, &:m))
    p(c.call(**h3, &:m))
    p(c.call(a: 1, **h2, &:m))

    c.singleton_class.remove_method(:m)
    def c.m; end
    p(c.call(**{}, &:m))
    p(c.call(**kw, &:m))
    begin; c.call(**h, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h2, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h3, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, **h2, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c.singleton_class.remove_method(:m)
    def c.m(args)
      args
    end
    begin; c.call(**{}, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**kw, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(c.call(**h, &:m))
    p(c.call(a: 1, &:m))
    p(c.call(**h2, &:m))
    p(c.call(**h3, &:m))
    p(c.call(a: 1, **h2, &:m))

    c.singleton_class.remove_method(:m)
    def c.m(**args)
      args
    end
    p(c.call(**{}, &:m))
    p(c.call(**kw, &:m))
    p(c.call(**h, &:m))
    p(c.call(a: 1, &:m))
    p(c.call(**h2, &:m))
    p(c.call(**h3, &:m))
    p(c.call(a: 1, **h2, &:m))
    begin; c.call(h, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(h2, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(h3, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c.singleton_class.remove_method(:m)
    def c.m(arg, **args)
      [arg, args]
    end
    begin; c.call(**{}, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**kw, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h2, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h3, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, **h2, &:m)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c.singleton_class.remove_method(:m)
    def c.m(arg=1, **args)
      [arg, args]
    end
    p(c.call(**{}, &:m))
    p(c.call(**kw, &:m))
    p(c.call(**h, &:m))
    p(c.call(a: 1, &:m))
    p(c.call(**h2, &:m))
    p(c.call(**h3, &:m))
    p(c.call(a: 1, **h2, &:m))