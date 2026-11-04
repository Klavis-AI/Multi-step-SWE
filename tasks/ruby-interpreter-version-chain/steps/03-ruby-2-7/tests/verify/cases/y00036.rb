begin
  def f(*a, **k); [a, k]; end
  p f(1, 2, x: 3)
rescue Exception => e
  p e.class
end
