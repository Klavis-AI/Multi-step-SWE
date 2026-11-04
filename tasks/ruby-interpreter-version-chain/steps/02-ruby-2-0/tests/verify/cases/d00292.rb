def f(a); a; end
begin
  p(f(d: 4))
rescue Exception => e
  p e.class
end
