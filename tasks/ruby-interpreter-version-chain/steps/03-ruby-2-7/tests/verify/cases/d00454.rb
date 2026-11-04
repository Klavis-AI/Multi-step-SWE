def f(a = 1, k: 2); [a, k]; end
begin
  p(f({'a' => 1}))
rescue Exception => e
  p e.class
end
