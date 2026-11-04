def f(a, **kw); [a, kw]; end
begin
  p(f({'a' => 1}))
rescue Exception => e
  p e.class
end
