def f(a = 1, k: 2); [a, k]; end
begin
  p(f(1, k: 2))
rescue Exception => e
  p e.class
end
