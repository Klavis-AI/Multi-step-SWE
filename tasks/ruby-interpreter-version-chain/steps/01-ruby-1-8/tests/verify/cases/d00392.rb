def f(*a); a; end
begin
  p(f(1))
rescue Exception => e
  p e.class
end
