def f(*a, **kw); [a, kw]; end
begin
  p(f(d: 4))
rescue Exception => e
  p e.class
end
