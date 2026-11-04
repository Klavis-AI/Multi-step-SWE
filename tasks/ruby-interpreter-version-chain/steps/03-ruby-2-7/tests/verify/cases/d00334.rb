def f(a, k: 1); [a, k]; end
begin
  p(f({'a' => 1}))
rescue Exception => e
  p e.class
end
