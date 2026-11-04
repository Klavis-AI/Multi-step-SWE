# frozen_string_literal: false
f = ->(**kw) { kw.merge(:a=>1) }
    g = ->(kw) { kw.merge(:a=>2) }

    p((f >> g).call(a: 3)[:a])
    begin; (f << g).call(a: 3)[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p((f >> g).call(a: 3)[:a])
    begin; (f << g).call({a: 3})[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; (f >> g).call({a: 3})[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p((g << f).call(a: 3)[:a])
    begin; (g >> f).call(a: 3)[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; (g << f).call({a: 3})[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; (g >> f).call({a: 3})[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; (f << g).call(**{})[:a]
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p((f >> g).call(**{})[:a])