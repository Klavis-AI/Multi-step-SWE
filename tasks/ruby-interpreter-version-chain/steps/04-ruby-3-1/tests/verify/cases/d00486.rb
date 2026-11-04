def f(a, b = 2, *c, d:, e: 5, **f); [a, b, c, d, e, f]; end
begin
  p(f(1, d: 4))
rescue Exception => e
  p e.class
end
