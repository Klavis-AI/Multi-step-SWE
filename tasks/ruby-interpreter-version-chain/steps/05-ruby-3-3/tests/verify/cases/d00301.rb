def f(k: 1); k; end
begin
  p(f({k: 1}))
rescue Exception => e
  p e.class
end
