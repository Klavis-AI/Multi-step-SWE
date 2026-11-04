def f(*a, **kw); [a, kw]; end
begin
  p(f(1, d: 4))
rescue Exception => e
  p e.class
end
