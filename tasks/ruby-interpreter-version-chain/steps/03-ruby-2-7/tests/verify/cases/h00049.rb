begin
  def f(a, k: 10, **r); [a, k, r]; end
  p f(1)
  p f(1, k: 2)
  p f(1, k: 2, z: 3)
rescue Exception => e
  p e.class
end
