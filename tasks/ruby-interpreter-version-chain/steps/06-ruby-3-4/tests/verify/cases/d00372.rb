def f(a, **kw); [a, kw]; end
begin
  p(f({k: 1}))
rescue Exception => e
  p e.class
end
