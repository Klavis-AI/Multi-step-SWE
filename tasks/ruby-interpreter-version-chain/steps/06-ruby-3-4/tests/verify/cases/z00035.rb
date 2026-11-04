begin
  def f(h); h; end
  p f({a: 1})
  def g(**k); k; end
  p g(**{a: 1})
rescue Exception => e
  p e.class
end
