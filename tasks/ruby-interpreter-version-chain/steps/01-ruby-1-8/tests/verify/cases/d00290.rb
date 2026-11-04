def f(a); a; end
begin
  p(f())
rescue Exception => e
  p e.class
end
