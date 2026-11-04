def f(*a, **kw); [a, kw]; end
begin
  p(f(**{}))
rescue Exception => e
  p e.class
end
