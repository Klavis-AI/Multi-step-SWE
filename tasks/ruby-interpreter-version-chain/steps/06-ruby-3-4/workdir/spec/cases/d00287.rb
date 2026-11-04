def f(a); a; end
begin
  p(f({'a' => 1}))
rescue Exception => e
  p e.class
end
