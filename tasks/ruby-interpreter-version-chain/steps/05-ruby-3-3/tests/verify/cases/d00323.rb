def f(a, k: 1); [a, k]; end
begin
  p(f(k: 1))
rescue Exception => e
  p e.class
end
