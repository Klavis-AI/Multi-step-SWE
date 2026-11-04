def f(a, k: 1); [a, k]; end
begin
  p(f(d: 4))
rescue Exception => e
  p e.class
end
