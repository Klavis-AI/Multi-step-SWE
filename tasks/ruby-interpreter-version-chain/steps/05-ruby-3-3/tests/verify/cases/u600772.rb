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
    def c.method_missing(_, *args)
      args
    end
    p(c.call(**{}, &:m2))
    p(c.call(**kw, &:m2))
    p(c.call(**h, &:m2))
    p(c.call(h, **{}, &:m2))
    p(c.call(a: 1, &:m2))
    p(c.call(**h2, &:m2))
    p(c.call(**h3, &:m2))
    p(c.call(a: 1, **h2, &:m2))

    c.singleton_class.remove_method(:method_missing)
    def c.method_missing(_) end
    p(c.call(**{}, &:m2))
    p(c.call(**kw, &:m2))
    begin; c.call(**h, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h2, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h3, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, **h2, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c.singleton_class.remove_method(:method_missing)
    def c.method_missing(_, args)
      args
    end
    begin; c.call(**{}, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**kw, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(c.call(**h, &:m2))
    p(c.call(a: 1, &:m2))
    p(c.call(**h2, &:m2))
    p(c.call(**h3, &:m2))
    p(c.call(a: 1, **h2, &:m2))

    c.singleton_class.remove_method(:method_missing)
    def c.method_missing(_, **args)
      args
    end
    p(c.call(**{}, &:m2))
    p(c.call(**kw, &:m2))
    p(c.call(**h, &:m2))
    p(c.call(a: 1, &:m2))
    p(c.call(**h2, &:m2))
    p(c.call(**h3, &:m2))
    p(c.call(a: 1, **h2, &:m2))
    begin; c.call(h, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(h2, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(h3, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c.singleton_class.remove_method(:method_missing)
    def c.method_missing(_, arg, **args)
      [arg, args]
    end
    begin; c.call(**{}, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**kw, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h2, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(**h3, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; c.call(a: 1, **h2, &:m2)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    c.singleton_class.remove_method(:method_missing)
    def c.method_missing(_, arg=1, **args)
      [arg, args]
    end
    p(c.call(**{}, &:m2))
    p(c.call(**kw, &:m2))
    p(c.call(**h, &:m2))
    p(c.call(a: 1, &:m2))
    p(c.call(**h2, &:m2))
    p(c.call(**h3, &:m2))
    p(c.call(a: 1, **h2, &:m2))