def f(k: 1); k; end
begin
  p(f({'a' => 1}))
rescue Exception => e
  p e.class
end
